#pragma once

#include "EmsTelegram.h"

namespace heating::ems {

class UBAOutdoorTemp : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x00D1;

    void logData() const;

	std::optional<int16_t> getOutdoorTemperature() const {
		auto temp = getValue<int16_t>(0);
		if (temp) {
			if (temp == INT16_MAX || temp == INT16_MIN) {
				return {};
			}
			//  DBGLOGEMS("UBAOutdoorTemp  %d\n", temp.value());
		}
		return temp;
	}

	static EmsTelegram getRequest(uint8_t deviceId, uint8_t destination) {
		return {EmsTelegram::operation_t::READ, deviceId, destination, 0, predefinedTypeId, {2}};
	}

};

}