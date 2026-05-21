#pragma once

#include "GpioPort.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace gpio {

class BuiltinGpioPort : public GpioPort {
public:
	explicit BuiltinGpioPort(uint8_t pin) : pin_(pin) {}

	void initOutput() override {
#ifdef ARDUINO
		pinMode(pin_, OUTPUT);
#endif
	}

	void write(bool high) override {
#ifdef ARDUINO
		digitalWrite(pin_, high ? HIGH : LOW);
#endif
	}

private:
	uint8_t pin_;
};

} // namespace gpio
