#pragma once

#include "EmsTelegram.h"

namespace heating::ems {

class UBAInternalWeatherCompensatedMode : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x0028;

	static EmsTelegram getRequest(uint8_t deviceId, uint8_t destination) {
		return {EmsTelegram::operation_t::READ, deviceId, destination, 0, predefinedTypeId, {maxEmsDataLength}};
	}

    void logData() const;

};

}