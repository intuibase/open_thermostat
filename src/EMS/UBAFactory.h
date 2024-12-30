#pragma once

#include "EmsTelegram.h"

namespace heating::ems {


class UBAFactory : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x0004;

	static EmsTelegram getRequest(uint8_t deviceId, uint8_t destination) {
		return {EmsTelegram::operation_t::READ, deviceId, destination, 0, predefinedTypeId, {maxEmsDataLength}};
	}

	void logData() const;

	auto getBoilerNominalPower() const {
		return getValue<uint8_t>(4);
	}

	auto getBurnerMinPower() const {
		return getValue<uint8_t>(5);
	}

	auto getBurnerMaxPower() const {
		return getValue<uint8_t>(6);
	}
};


}