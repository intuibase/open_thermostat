#pragma once

#include "EmsTelegram.h"

namespace heating::ems {

class UBAMonitorSlowPlus2 : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x00E3;

	void logData() const;

	auto getPumpVenting() const {
		return getValue<uint8_t>(6);
	}

};


}