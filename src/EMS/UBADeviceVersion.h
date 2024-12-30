#pragma once

#include "EmsTelegram.h"

namespace heating::ems {

class UBADeviceVersion : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x0002;

	static EmsTelegram getRequest(uint8_t deviceId, uint8_t destination) {
		return {EmsTelegram::operation_t::READ, deviceId, destination, 0, predefinedTypeId, {maxEmsDataLength}};
	}

	static EmsTelegram getResponse(uint8_t deviceId, uint8_t destination, uint8_t offset, uint8_t requestedSize) {
		constexpr uint8_t vMajor = 1;
		constexpr uint8_t vMinor = 1;
		constexpr uint8_t vVendorId = 99;
		constexpr uint8_t vProdId = 99;
		return {EmsTelegram::operation_t::WRITE, deviceId, destination, 0, predefinedTypeId, {vProdId, vMajor, vMinor, 0, 0, 0, 0, 0, 0, vVendorId}};
	}

	void logData() const;
};

}