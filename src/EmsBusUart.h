#pragma once

#include "Logging.h"

#include <driver/uart.h>
#include <hal/uart_types.h>
#include <soc/uart_reg.h>
#include <array>
#include <atomic>

#include <functional>

#include "EmsBusUartForwarder.h"

namespace heating {


namespace uart_detail {
static void uart_event_task(void *pvParameters);
}

class EmsBusUart {
public:
	ib::logger::LoggerInterface::LogFeatureType getLogFeature() const { return logFeature_; }
	std::shared_ptr<ib::logger::LoggerInterface> getLogger() const { return log_; }
	using processTelegram_t = std::function<void(uint8_t *, uint8_t)>;
	static constexpr int EmsBusBaudrate = 9600;
	static constexpr uart_port_t UartSlot = UART_NUM_2;
	static constexpr size_t EmsMaxTelegramSize = 33;

	EmsBusUart(std::shared_ptr<ib::logger::LoggerInterface> log, processTelegram_t processTelegram) : log_(log), processTelegram_(processTelegram), forwarder_(std::move(log)) {
		if (log_) {
			logFeature_ = log_->addFeature("EmsUart");
		}
	}

	void start(uint8_t rxPin, uint8_t txPin) {
		// params copied from EMS-ESP32
		rxPin_ = rxPin;
		txPin_ = txPin;

		uart_config_t uart_config = {
			.baud_rate  = EmsBusBaudrate,
			.data_bits  = UART_DATA_8_BITS,
			.parity     = UART_PARITY_DISABLE,
			.stop_bits  = UART_STOP_BITS_1,
			.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
			.source_clk = UART_SCLK_APB,
		};

		uart_param_config(UartSlot, &uart_config);
		uart_set_pin(UartSlot, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
		uart_driver_install(UartSlot, 129, 0, (EmsMaxTelegramSize + 1) * 2, &uartQueue_, 0);
		uart_set_rx_full_threshold(UartSlot, 1);
		uart_set_rx_timeout(UartSlot, 0);

		xTaskCreate(heating::uart_detail::uart_event_task, "EmsBusUart", 2560, this, configMAX_PRIORITIES - 1, NULL);
		uart_enable_intr_mask(UartSlot, UART_BRK_DET_INT_ENA | UART_RXFIFO_FULL_INT_ENA);

		DBGLOGFD(log_, logFeature_, "Started rx: %d tx: %d\n", rxPin, txPin);
	}

	void startForwarder(uint8_t rxPin, uint8_t txPin) {
		forwarder_.start(rxPin, txPin);
	}

#define EMSUART_TX_BIT_TIME 104 // bit time @9600 baud
#define EMSUART_TX_WAIT_HT3 (EMSUART_TX_BIT_TIME * 17) // 1768
#define EMSUART_TX_BRK_HT3 (EMSUART_TX_BIT_TIME * 11)

	bool writeToEms(std::vector<uint8_t> const &data) { return writeToEms(data.data(), static_cast<uint8_t>(data.size())); }

	bool writeToEms(uint8_t const *data, uint8_t length) {
		if (length == 0 || length > EmsMaxTelegramSize) {
			return false;
		}

        for (uint8_t i = 0; i < length; i++) {
            uart_write_bytes(UartSlot, &data[i], 1);
            delayMicroseconds(EMSUART_TX_WAIT_HT3);
        }
        uart_set_line_inverse(UartSlot, UART_SIGNAL_TXD_INV);
        delayMicroseconds(EMSUART_TX_BRK_HT3);
        uart_set_line_inverse(UartSlot, 0);

		return true;
	}

	void requestReset() { resetRequested_.store(true, std::memory_order_relaxed); }

	uint32_t forwarderErrors() const { return forwarder_.rxErrors.load(std::memory_order_relaxed); }
	uint32_t forwarderDropped() const { return forwarder_.dropped.load(std::memory_order_relaxed); }
	std::atomic<uint32_t> rxErrors{0};
	std::atomic<uint32_t> rxTimeouts{0};
	std::atomic<uint32_t> resets{0};

private:
	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_{};
	QueueHandle_t getQueueHandle() {
		return uartQueue_;
	}

	constexpr uart_port_t getUartSlot() {
		return UartSlot;
	}

	void processTelegram(size_t size) {
		bool performProcess = true;

		if (forwarder_.isEnabled() && size == 2 && !(buffer_[0] & 0x80) ) { // polling if 0 byte is not masked, if masked then it is poll response
			// DBGLOGFD(log_, logFeature_, "Polling device %d\n", buffer_[0]);
			auto data = forwarder_.getDataToWriteToEmsBus(buffer_[0]); // we have data from that id, send
			if (data) {
				writeToEms(data->data.data(), data->length);
				performProcess = false; // it was a poll to forwarded device, we responded, so this poll is definitely not for us
			}
		}

		if (performProcess) {
			processTelegram_(buffer_.data(), size);
		}

		if (forwarder_.isEnabled() && size > 1) {
			forwarder_.writeToEms(buffer_.data(), size - 1);
		}
	}

	uint8_t *getDataBuffer() {
		return buffer_.data();
	}

	uint32_t getDataBufferSize() {
		return static_cast<uint32_t>(buffer_.size());
	}

	// Recovery runs only in the UART task, between complete event handlers.
	void performReset() {
		uart_flush_input(UartSlot);
		xQueueReset(uartQueue_);
		uart_send_break(UartSlot);
		resets.fetch_add(1, std::memory_order_relaxed);
	}

	std::atomic<bool> resetRequested_{false};
	QueueHandle_t uartQueue_;
	std::array<uint8_t, EmsMaxTelegramSize> buffer_;
	processTelegram_t processTelegram_;
	uint8_t rxPin_;
	uint8_t txPin_;

	EmsBusUartForwarder forwarder_;

	friend void heating::uart_detail::uart_event_task(void *pvParameters);
};

namespace uart_detail {
static void uart_event_task(void *pvParameters) {
	EmsBusUart *bus = static_cast<heating::EmsBusUart *>(pvParameters);
	uart_event_t event;
	uint32_t length = 0;

	constexpr auto rxTimeoutMs = 5000;

	for (;;) {
		if (bus->resetRequested_.exchange(false, std::memory_order_relaxed)) {
			bus->performReset();
			length = 0;
		}
		if (!xQueueReceive(bus->getQueueHandle(), &event, pdMS_TO_TICKS(rxTimeoutMs))) {
			bus->rxTimeouts.fetch_add(1, std::memory_order_relaxed);
			bus->requestReset();
			continue;
		}
		switch (event.type) {
		case UART_DATA:
			length += event.size;
			break;
		case UART_BREAK: {
			// Events describe bytes already buffered by the driver. Never wait for
			// missing bytes: discard a partial/oversized frame and resync at BREAK.
			const bool validSize = length > 0 && length <= bus->getDataBufferSize();
			bool complete = validSize;
			while (length > 0) {
				const auto chunk = std::min(length, bus->getDataBufferSize());
				const int read = uart_read_bytes(bus->getUartSlot(), bus->getDataBuffer(), chunk, 0);
				if (read != static_cast<int>(chunk)) {
					complete = false;
					uart_flush_input(bus->getUartSlot());
					xQueueReset(bus->getQueueHandle());
					break;
				}
				length -= chunk;
				if (complete) {
					bus->processTelegram(chunk);
				}
			}
			if (!complete) {
				bus->rxErrors.fetch_add(1, std::memory_order_relaxed);
			}
			length = 0;
			break;
		}
		case UART_FIFO_OVF:
		case UART_BUFFER_FULL:
			bus->rxErrors.fetch_add(1, std::memory_order_relaxed);
			uart_flush_input(bus->getUartSlot());
			xQueueReset(bus->getQueueHandle());
			length = 0;
			break;
		case UART_PARITY_ERR:
		case UART_FRAME_ERR:
			bus->rxErrors.fetch_add(1, std::memory_order_relaxed);
			break;
		default:
			break;
		}
	}
	vTaskDelete(nullptr);
}



}

}
