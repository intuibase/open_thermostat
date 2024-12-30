#include "EmsBusUartForwarder.h"


namespace heating {

void EmsBusUartForwarder::start(uint8_t rxPin, uint8_t txPin) {
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

	xTaskCreate(heating::uart_detail::uart_forwarder_event_task, "EmsBusUartForwarder", 2048, this, configMAX_PRIORITIES - 1, NULL);
	uart_enable_intr_mask(UartSlot, UART_BRK_DET_INT_ENA | UART_RXFIFO_FULL_INT_ENA);

	DBGLOGUARTFW("Started rx: %d tx: %d\n", rxPin, txPin);

	enabled_ = true;
}



}
