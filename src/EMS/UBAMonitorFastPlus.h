#pragma once

#include "EmsTelegram.h"
#include <optional>
#include <array>

namespace heating::ems {

class UBAMonitorFastPlus : public EmsTelegram {
public:
	static constexpr uint16_t predefinedTypeId = 0x00E4;

	void logData() const;

	auto getDisplayCode() const -> std::optional<std::array<char, 3>> {
		if (offset_ != 0 || data_.size() < 3) {
			return std::optional<std::array<char, 3>>{};
		}
		return std::optional<std::array<char, 3>>(std::array<char, 3>{ static_cast<char>(data_[1]), static_cast<char>(data_[2]), 0});
	}

	auto getServiceCode() const {
		return getValue<uint16_t>(4);
	}

	auto getSelectedFlowTemperature() const {
		return getValue<uint8_t>(6);
	}
	auto getCurrentFlowTemperature() const {
		return getValue<uint16_t>(7);
	}
	auto getBurningGas() const {
		return getValue<bool, 0>(11);
	}
	auto getPumpEnabled() const {
		return getValue<bool, 1>(11);
	}
	auto get3WayValeEnabled() const {
		return getValue<bool, 2>(11);
	}
	auto getPressure() const {
		return getValue<uint8_t>(21);
	}
	auto getCurrentBurnerPower() const {
		return getValue<uint8_t>(10);
	}

	auto getSiphonFilling() const {
		return getValue<bool, 6>(11);
	}

	std::optional<bool> getHeatingActive() const {
		auto gas = getBurningGas();
		if (!gas) {
			return std::optional<bool>{};
		}
		// return std::optional<bool>{getBurningGas().value() && getPumpEnabled().value()};
		return std::optional<bool>{!get3WayValeEnabled().value() && getPumpEnabled().value()};
	}

	std::optional<bool> getWarmWaterActive() const {
		auto gas = getBurningGas();
		if (!gas) {
			return std::optional<bool>{};
		}
		return std::optional<bool>{getBurningGas().value() && get3WayValeEnabled().value()};
	}
};

}