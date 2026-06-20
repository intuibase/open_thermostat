#pragma once

#include "GpioPort.h"

#ifdef ARDUINO
#include <Wire.h>
#endif

#include <cstdint>
#include <memory>

namespace gpio {

// Shared state for one PCF8574 (8-pin) or PCF8575 (16-pin) device on the I2C bus.
// PCF pins are quasi-bidirectional — they default HIGH. Writing LOW drives the pin low.
// All pins must be written at once, so we keep shadow state here.
struct PcfDevice {
	uint8_t address;
	bool is16bit;       // true = PCF8575 (16 pins), false = PCF8574/8574a (8 pins)
	uint16_t shadow = 0xFFFF; // start with all HIGH (default PCF state)

	PcfDevice(uint8_t addr, bool wide) : address(addr), is16bit(wide), shadow(0xFFFF) {
		// Drive all pins HIGH immediately — active-low relays: HIGH = coil OFF (safe default).
		flush();
	}

	void writePin(uint8_t pin, bool high) {
		if (high)
			shadow |= static_cast<uint16_t>(1u << pin);
		else
			shadow &= static_cast<uint16_t>(~(1u << pin));
		flush();
	}

	void flush() {
#ifdef ARDUINO
		Wire.beginTransmission(address);
		Wire.write(static_cast<uint8_t>(shadow & 0xFF));
		if (is16bit)
			Wire.write(static_cast<uint8_t>((shadow >> 8) & 0xFF));
		Wire.endTransmission();
#endif
	}
};

class PcfGpioPort : public GpioPort {
public:
	PcfGpioPort(std::shared_ptr<PcfDevice> device, uint8_t pin)
		: device_(std::move(device)), pin_(pin) {}

	void initOutput() override {
		// Active-low relay: write HIGH → relay coil OFF (safe default).
		device_->writePin(pin_, true);
	}

	void write(bool high) override {
		device_->writePin(pin_, !high); // invert: active-low relay — HIGH=off, LOW=on
	}

private:
	std::shared_ptr<PcfDevice> device_;
	uint8_t pin_;
};

} // namespace gpio
