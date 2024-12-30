#pragma once

#include "EmsTelegram.h"

namespace heating::ems {

class UBAMonitorWWPlus : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x00E9;

	void logData() const;

	auto getFlow() const {
		return getValue<uint8_t>(11);
	}

	auto getCurrentTemperature() const {
		return getValue<uint16_t>(1);
	}
};



}