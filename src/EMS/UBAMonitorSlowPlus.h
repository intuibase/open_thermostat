#pragma once

#include "EmsTelegram.h"

namespace heating::ems {

class UBAMonitorSlowPlus : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x00E5;

	void logData() const;

	auto getFanEnabled() const {
		return getValue<bool, 2>(2);
	}

	auto getPumpEnabled() const {
		return getValue<bool, 5>(2);
	}

};


}