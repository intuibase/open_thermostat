#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace heating {

struct RoomConfig {
	using BleAddress_t = std::array<uint8_t, 6>;

	struct TemperatureSetting {
		bool doesFit(uint16_t time, uint8_t dayOfTheWeek) const;
		bool isEnabled() const;
		int16_t getTemperature() const;
		std::optional<uint8_t> getHeatingTemperatureOverride() const;

		std::string name_;
		uint16_t timeFrom_; // HHMM - 23:30 -> 2300
		uint16_t timeTo_;   // HHMM
		int16_t temperature_ = 0;
		std::optional<uint8_t> heatingTemperatureOverride_; // heating temperature set on boiler
		bool enabled_ = true;
		std::vector<std::string> valves_;
		std::array<bool, 7> days_ = {{true, true, true, true, true, true, true}}; // TODO memory use bitfields
	};

	bool enabled_ = false;

	uint16_t baseTemperature_ = 2100;  // 21.00 deg
	uint8_t temperatureMarginUp_ = 20; // 0.2deg
	uint8_t temperatureMarginDown_ = 20;
	std::string name_; // TODO memory limit to 15 to avoid allocation?

	BleAddress_t sensorAddress_;

	std::vector<TemperatureSetting> temperatures_;
	std::vector<std::string> valves_;
};

}