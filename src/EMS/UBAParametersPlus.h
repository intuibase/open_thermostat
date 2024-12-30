#pragma once

#include "EmsTelegram.h"

namespace heating::ems {


class UBAParametersPlus : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x00E6;

	static EmsTelegram getRequest(uint8_t deviceId, uint8_t destination) {
		return {EmsTelegram::operation_t::READ, deviceId, destination, 0, predefinedTypeId, {maxEmsDataLength}};
	}

	void logData() const;

	auto getHeatingEnabled() const {
		return getValue<uint8_t>(0);
	}

	auto getHeatingTemperature() const {
		return getValue<uint8_t>(1);
	}

	auto getMaximumHeatingTemperature() const {
		return getValue<uint8_t>(3);
	}

	static EmsTelegram setHeatingTemperature(uint8_t deviceId, uint8_t destination, uint8_t temp) {
		return {EmsTelegram::operation_t::WRITE, deviceId, destination, 1, predefinedTypeId, {temp}};
	}
};


}