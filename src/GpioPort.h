#pragma once

#include <cstdint>

namespace gpio {

// Abstract interface for a single GPIO output pin.
// Implementations: BuiltinGpio (ESP32 native), PcfGpio (PCF8574 I2C), McpGpio (MCP23017 I2C)
class GpioPort {
public:
	virtual ~GpioPort() = default;

	virtual void initOutput() = 0;
	virtual void write(bool high) = 0;
};

// Null implementation for pins that are not used (e.g. boiler pin in EMS mode)
class NullGpioPort : public GpioPort {
public:
	void initOutput() override {}
	void write(bool) override {}
};

} // namespace gpio
