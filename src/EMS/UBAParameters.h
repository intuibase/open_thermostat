#pragma once

#include "EmsTelegram.h"

namespace heating::ems {

class UBAParameters : public EmsTelegram { // Valid for EMS1.0 boilers, empty for BOSCH 2300
public:
	static constexpr uint16_t predefinedTypeId = 0x0016;

	static EmsTelegram getRequest(uint8_t deviceId, uint8_t destination) {
		return {EmsTelegram::operation_t::READ, deviceId, destination, 0, predefinedTypeId, {maxEmsDataLength}};
	}
	void logData() const;
};


}
