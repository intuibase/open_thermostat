#pragma once

#include "EmsTelegram.h"

namespace heating::ems {

class UBAParametersWWPlus : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x00EA;

	static EmsTelegram getRequest(uint8_t deviceId, uint8_t destination) {
		return {EmsTelegram::operation_t::READ, deviceId, destination, 0, predefinedTypeId, {maxEmsDataLength}};
	}

	void logData() const;

	auto getWarmWaterEnabled() const {
		return getValue<uint8_t>(5);
	}
	auto getSelectedWarmWaterTemperature() const {
		return getValue<uint8_t>(6);
	}

	auto getECOEnabled() const {
		return getValue<uint8_t>(26);
	}
};



}