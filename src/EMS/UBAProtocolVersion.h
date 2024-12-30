#pragma once

#include "EmsTelegram.h"

namespace heating::ems {

class UBAProtocolVersion : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x00EF;

	static EmsTelegram getRequest(uint8_t deviceId, uint8_t destination) {
		return {EmsTelegram::operation_t::READ, deviceId, destination, 0, predefinedTypeId, {maxEmsDataLength}};
	}

	auto getProtocolVersion() const {
		return getValue<uint8_t>(0);
	}

	void logData() const;
};

}