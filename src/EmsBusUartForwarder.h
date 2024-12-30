#pragma once

#include "Logger.h"
#include <stdint.h>

#include <driver/uart.h>
#include <hal/uart_types.h>
#include <soc/uart_reg.h>
#include <array>

#include <functional>
#include <mutex>
#include <unordered_map>
#include <queue>

namespace heating {

namespace uart_detail {
static void uart_forwarder_event_task(void *pvParameters);
}

class EmsBusUartForwarder {
public:
	static constexpr int EmsBusBaudrate = 9600;
	static constexpr int UartSlot = 1;
	static constexpr size_t EmsMaxTelegramSize = 33;

	static constexpr size_t MaxForwarderQueueSize = 10;

	void start(uint8_t rxPin = 13, uint8_t txPin = 5);

	bool isEnabled() {
		return enabled_;
	}

#define EMSUART_TX_BIT_TIME 104 // bit time @9600 baud
#define EMSUART_TX_WAIT_HT3 (EMSUART_TX_BIT_TIME * 17) // 1768
#define EMSUART_TX_BRK_HT3 (EMSUART_TX_BIT_TIME * 11)

	bool writeToEms(std::vector<uint8_t > data) {
		return writeToEms(data.data(), static_cast<uint8_t>(data.size()));
	}

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

	std::shared_ptr<std::vector<uint8_t>> getDataToWriteToEmsBus(uint8_t id) {
		std::lock_guard<std::mutex> m(sendToEmsBusQueueMutex_);
		auto found = sendToEmsBusQueue_.find(id);
		if (found == std::end(sendToEmsBusQueue_)) {
			return {};
		}

		if (found->second.empty()) {
			return {};
		}

		auto data = std::move(found->second.front());
		found->second.pop();

		DBGLOGUARTFW("Found telegram in 0x%2.2X queue, %d telegrams left\n", id, found->second.size());

		return data;
	}

private:
	QueueHandle_t getQueueHandle() {
		return uartQueue_;
	}

	constexpr int getUartSlot() {
		return UartSlot;
	}

	uint8_t *getDataBuffer() {
		return buffer_.data();
	}

	uint32_t getDataBufferSize() {
		return static_cast<uint32_t>(buffer_.size());
	}

	void enqueueWriteToEmsBus(std::shared_ptr<std::vector<uint8_t>> data) {
		std::lock_guard<std::mutex> m(sendToEmsBusQueueMutex_);
		uint8_t id = data->at(0) & 0x7F;
		DBGLOGUARTFW("Enqueued telegram from 0x%2.2X. Size: %d\n", id, data->size());
		DBGLOGUARTFW("sendToEmsBusQueue Id's: %d. Queue for id 0x%2.2X: %d\n", sendToEmsBusQueue_.size(), id, sendToEmsBusQueue_[id].size());

		if (sendToEmsBusQueue_[id].size() == MaxForwarderQueueSize) {
			DBGLOGUARTFW("Can't enqueue telegram from 0x%2.2X. Size: %d. Queue full (%d)\n", id, data->size(), sendToEmsBusQueue_[id].size());
			heating::logger.printf("EmsBus Free memory %d/%d (minimum was: %d) MaxAlloc: %d Stack remaining: %d UpTime: %lds\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), uxTaskGetStackHighWaterMark(nullptr), millis()/1000);
			return;
		}

		sendToEmsBusQueue_[id].push(std::move(data));
	}

	bool enabled_ = false;

	QueueHandle_t uartQueue_;
	std::array<uint8_t, EmsMaxTelegramSize> buffer_;

	using deviceTxQueue_t = std::queue<std::shared_ptr<std::vector<uint8_t>>>;

	std::unordered_map<uint8_t, deviceTxQueue_t> sendToEmsBusQueue_;
	std::mutex sendToEmsBusQueueMutex_;

	friend void heating::uart_detail::uart_forwarder_event_task(void *pvParameters);
};

namespace uart_detail {
static void uart_forwarder_event_task(void *pvParameters) {
	EmsBusUartForwarder *bus = static_cast<heating::EmsBusUartForwarder *>(pvParameters);
	uart_event_t event;
	uint32_t length = 0;

	for (;;) {
		if (xQueueReceive(bus->getQueueHandle(), (void *)&event, (TickType_t)portMAX_DELAY)) {
			switch (event.type) {
			case UART_DATA:
				length += event.size;
				break;
			case UART_BREAK: // we got some data from EMS-ESP32, need to send them to EMS bus
				if (length > bus->getDataBufferSize()) {
					// read trash data
					while(length > 0) {
						DBGLOGUARTFW("Forwarder message too long, reading %ld/%ld\n", std::min(length, bus->getDataBufferSize()), length);
						uart_read_bytes(bus->getUartSlot(), bus->getDataBuffer(), std::min(length, bus->getDataBufferSize()), portMAX_DELAY);
						length -= std::min(length, bus->getDataBufferSize());
					}
				} else {
					if (uart_read_bytes(bus->getUartSlot(), bus->getDataBuffer(), length, portMAX_DELAY) != -1) {
						auto dataToEnqueue = std::make_shared<std::vector<uint8_t>>(bus->getDataBuffer(), bus->getDataBuffer() + length - 1);
						bus->enqueueWriteToEmsBus(std::move(dataToEnqueue));
					} else {
						DBGLOGUARTFW("read failed\n");
					}
				}
				length = 0;
				break;
			case UART_FIFO_OVF:
				DBGLOGUARTFW("forwarder event fifo overflow\n", "");
				uart_flush_input(bus->getUartSlot());
				xQueueReset(bus->getQueueHandle());
				length = 0;
				break;
			//Event of UART ring buffer full
			case UART_BUFFER_FULL:
				DBGLOGUARTFW("forwarder event buffer full\n", "");
				// If buffer full happened, you should consider increasing your buffer size
				// As an example, we directly flush the rx buffer here in order to read more data.
				uart_flush_input(bus->getUartSlot());
				xQueueReset(bus->getQueueHandle());
				length = 0;
				break;
			default:
				DBGLOGUARTFW("Forwarder event %d\n", event.type);
				break;
			}
		}
	}
	vTaskDelete(nullptr);
}



}

}