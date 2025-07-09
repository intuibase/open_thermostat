#pragma once

#include "Logger.h"
#include <stdint.h>

#include <driver/uart.h>
#include <hal/uart_types.h>
#include <soc/uart_reg.h>
#include <array>

#include <functional>

#include "EmsBusUartForwarder.h"

namespace heating {


namespace uart_detail {
static void uart_event_task(void *pvParameters);
}

class EmsBusUart {
public:
	using processTelegram_t = std::function<void(uint8_t *, uint8_t)>;
	static constexpr int EmsBusBaudrate = 9600;
	static constexpr int UartSlot = 2;
	static constexpr size_t EmsMaxTelegramSize = 33;

	EmsBusUart(processTelegram_t processTelegram) : processTelegram_(processTelegram) {
	}

	void start(uint8_t rxPin, uint8_t txPin) {
		// params copied from EMS-ESP32

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

		DBGLOGUART("Started rx: %d tx: %d\n", rxPin, txPin);
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

	void reset() {
		DBGLOGUART("reset\n");
		uart_wait_tx_done(UartSlot, (TickType_t)1000 / portTICK_PERIOD_MS); //(UART_NUM_MAX -1));
		uart_flush(UartSlot);
		xQueueReset(uartQueue_);
		uart_send_break(UartSlot);
	}

private:
	QueueHandle_t getQueueHandle() {
		return uartQueue_;
	}

	constexpr int getUartSlot() {
		return UartSlot;
	}

	void processTelegram(size_t size) {
		bool performProcess = true;

		if (forwarder_.isEnabled() && size == 2 && !(buffer_[0] & 0x80) ) { // polling if 0 byte is not masked, if masked then it is poll response
			// DBGLOGUART("Polling device %d\n", buffer_[0]);
			auto data = forwarder_.getDataToWriteToEmsBus(buffer_[0]); // we have data from that id, send
			if (data) {
				DBGLOGUART("We have data forwarded from device 0x%2.2X. Size %d. Sending to EMSBUS\n", buffer_[0], data->size());
				writeToEms(data->data(), data->size());
				performProcess = false; // it was a poll to forwarded device, we responded, so this poll is definitely not for us
			}
		}

		if (performProcess) {
			processTelegram_(buffer_.data(), size);
		}

		if (forwarder_.isEnabled()) {
			forwarder_.writeToEms(buffer_.data(), size - 1);
		}
	}

	uint8_t *getDataBuffer() {
		return buffer_.data();
	}

	uint32_t getDataBufferSize() {
		return static_cast<uint32_t>(buffer_.size());
	}

	QueueHandle_t uartQueue_;
	std::array<uint8_t, EmsMaxTelegramSize> buffer_;
	processTelegram_t processTelegram_;

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
		if (xQueueReceive(bus->getQueueHandle(), (void *)&event, (TickType_t)rxTimeoutMs / portTICK_PERIOD_MS)) { //(TickType_t)portMAX_DELAY)) {
			switch (event.type) {
			//Event of UART receving data
			/*We'd better handler data event fast, there would be much more data events than
			other types of events. If we take too much time on data event, the queue might
			be full.*/
			case UART_DATA:
				length += event.size;
				break;
			//Event of HW FIFO overflow detected
			//Event of UART RX break detected
			case UART_BREAK:
				// ESP_LOGI(TAG, "uart rx break");
				if (length > bus->getDataBufferSize()) {
					// read trash data
					while(length > 0) {
						DBGLOGUART("Message too long, reading %ld/%ld\n", std::min(length, bus->getDataBufferSize()), length);
						uart_read_bytes(bus->getUartSlot(), bus->getDataBuffer(), std::min(length, bus->getDataBufferSize()), portMAX_DELAY);
						length -= std::min(length, bus->getDataBufferSize());
					}
				} else {
					if (uart_read_bytes(bus->getUartSlot(), bus->getDataBuffer(), length, portMAX_DELAY) != -1) {
						bus->processTelegram(length);
					} else {
						DBGLOGUARTFW("read failed\n");
					}
				}
				length = 0;
				break;
			case UART_FIFO_OVF:
				DBGLOGUART("Event fifo overflow\n", "");
				// If fifo overflow happened, you should consider adding flow control for your application.
				// The ISR has already reset the rx FIFO,
				// As an example, we directly flush the rx buffer here in order to read more data.
				uart_flush_input(bus->getUartSlot());
				xQueueReset(bus->getQueueHandle());
				length = 0;
				break;
			//Event of UART ring buffer full
			case UART_BUFFER_FULL:
				DBGLOGUART("Event buffer full\n", "");
				// If buffer full happened, you should consider increasing your buffer size
				// As an example, we directly flush the rx buffer here in order to read more data.
				uart_flush_input(bus->getUartSlot());
				xQueueReset(bus->getQueueHandle());
				length = 0;
				break;
			case UART_PARITY_ERR:
				DBGLOGUART("parity check error\n", "");
				break;
			case UART_FRAME_ERR:
				DBGLOGUART("frame error\n", "");
				break;
			//Others
			default:
				DBGLOGUART("Event %d\n", event.type);
				break;
			}
		} else {
			DBGLOGUART("Read queue timeout\n");
			bus->reset();
		}
	}
	vTaskDelete(nullptr);
}



}

}
