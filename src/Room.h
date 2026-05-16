#pragma once

#include "RoomConfig.h"
#include "BeaconBleAddress.h"
#include "CircularBuffer.h"
#include "Logger.h"

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <time.h>
#include <mutex>
#include <tuple>
#include <optional>
#include <vector>

namespace heating {
class Room {
public:
	using clock_t = std::chrono::steady_clock;

	enum class TemperatureStatus : uint8_t { MISSING_TEMPERATURE, TEMPERATURE_OK, START_HEATING, CONTINUE_HEATING };

	Room(RoomConfig config) : config_(std::move(config)) {}

	Room(Room &&r) = default;
	Room &operator=(Room &&r) = default;
	Room &operator=(const Room &r) = delete;
	Room(const Room &r) = delete;
	using temperatureData_t = std::tuple<std::chrono::steady_clock::time_point, int16_t>; // temp

	class TemporaryOverride {
	public:
		TemporaryOverride(int16_t temperature, uint32_t lifeTimeSecs) : temperature_(temperature), lifeTime_(std::chrono::seconds(lifeTimeSecs)), activation_(std::chrono::steady_clock::now()) {}

		bool isValid() const { return std::chrono::steady_clock::now() < activation_ + lifeTime_; }

		uint32_t getSecondsLeft() const {
			auto now = std::chrono::steady_clock::now();
			auto remaining = std::chrono::duration_cast<std::chrono::seconds>((activation_ + lifeTime_) - now);
			return remaining.count() > 0 ? static_cast<uint32_t>(remaining.count()) : 0;
		}

		int16_t getTemperature() const { return temperature_; }

	private:
		int16_t temperature_;
		std::chrono::seconds lifeTime_;
		std::chrono::steady_clock::time_point activation_;
	};

	void storeTemperature(int16_t temperature);
	void storeBattery(int8_t batteryLevel);
	void storeHumidity(int16_t humidity);

	void createTemporaryOverride(int16_t temperature, uint32_t validSeconds);

	std::tuple<TemperatureStatus, std::optional<uint8_t>> shouldStartBoilerAndHeat();

	bool isEnabled() const;

	const std::string &getName() const { return config_.name_; }

	auto getValves() const { return config_.valves_; }
	auto const &getSensorAddress() const { return config_.sensorAddress_; }

	std::string getStatus() const;
	void getStatus(std::ostream &ss) const;


private:
	bool isTemperatureValid() const;
	std::pair<int16_t, const RoomConfig::TemperatureSetting *> getTemperatureSet(uint16_t currentTime, uint8_t dayOfTheWeek) const; // HH:MM 	// returns temperature set for current time - maximum one from all, but always overrides base temp
	int16_t getTemperatureMarginUp() const;
	int16_t getTemperatureMarginDown() const;
	std::pair<uint16_t, uint8_t> getTimeNow() const;
	std::optional<int16_t> getAverageTemperature() const;

	auto getMaxSampleAgeMs() const { return std::chrono::minutes(3); }

	bool debugLog_ = true;
	RoomConfig config_;
	std::unique_ptr<TemporaryOverride> temporaryOverride_;
	ib::CircularBuffer<temperatureData_t, 10> temperatureData_;
	mutable std::mutex mutex_;

	//	// statistical data
	std::atomic<int8_t> batteryLevel_ = -1;
	std::atomic<int16_t> currentHumidity_ = std::numeric_limits<decltype(currentHumidity_)>::min(); // humidity 6005 - 60.05%

	struct Statistics {
		const RoomConfig::TemperatureSetting *currentProgram_ = nullptr;
		bool shouldStartBoiler_ = false;
		bool shouldHeat_ = false;
	} stats;
};
}
