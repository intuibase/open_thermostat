#pragma once

#include "AutoLock.h"
#include "BeaconBleAddress.h"
#include "CircularBuffer.h"
#include "Logger.h"

#include <sstream>
#include <string>
#include <time.h>
#include <tuple>
#include <optional>
#include <vector>
#include <Arduino.h>

namespace heating {
class Room {
public:
	Room() = default;
	Room(Room &&r) = default;
	Room &operator=(Room &&r) = default;
	Room &operator=(const Room &r) = delete;
	Room(const Room &r) = delete;
	using temperatureData_t = std::tuple<unsigned long, int16_t>; // millis of read, temp

	struct TemperatureSetting {
		bool doesFit(std::string const &time, uint8_t dayOfTheWeek) const;
		bool isEnabled() const;
		int16_t getTemperature() const;
		std::optional<uint8_t> getHeatingTemperatureOverride() const;

		std::string name_;
		std::string timeFrom_; // HH:MM
		std::string timeTo_; // // HH:MM
		int16_t temperature_ = 0;
		std::optional<uint8_t> heatingTemperatureOverride_;
		bool enabled_ = true;
		std::vector<uint8_t> valves_;
		std::array<bool, 7> days_ = {{true, true, true, true, true, true, true}}; // TODO memory use bitfields
	};

	class TemporaryOverride {
	public:
		TemporaryOverride(int16_t temperature, uint32_t lifeTimeSecs) : temperature_(temperature), lifeTimeSecs_(lifeTimeSecs) {
		}

		bool isValid() const {
			return activation_ + lifeTimeSecs_ * 1000 > millis();
		}

		uint32_t getSecondsLeft() {
			return ((activation_ + lifeTimeSecs_ * 1000) - millis()) / 1000;
		}

		int16_t getTemperature() const {
			return temperature_;
		}

	private:
		unsigned long activation_ = millis();
		int16_t temperature_ = 0;
		uint32_t lifeTimeSecs_;
	};

	struct Statistics {
		const TemperatureSetting *currentProgram_ = nullptr;
		bool shouldStartBoiler_ = false;
		bool shouldHeat_ = false;
	} stats;


	std::vector<uint8_t> const &getValves() {
		AutoLock lock(mutex_, portMAX_DELAY);
		return valves_;

		//TODO get valves from current temp if exists
	}

	void storeTemperature(int16_t temperature);
	void storeBattery(int8_t batteryLevel);
	void storeHumidity(int16_t humidity);

	void createTemporaryOverride(int16_t temperature, uint32_t validSeconds);

	std::tuple<bool, bool, std::optional<uint8_t>> shouldStartBoilerAndHeat();
	bool isEnabled() const;

	std::string getStatus() const;
	void getStatus(std::ostream &ss) const;


private:
	bool isTemperatureValid() const;
	std::pair<int16_t, const Room::TemperatureSetting *> getTemperatureSet(std::string const &currentTime, uint8_t dayOfTheWeek) const; // HH:MM 	// returns temperature set for current time - maximum one from all, but always overrides base temp
	int16_t getTemperatureMarginUp() const;
	int16_t getTemperatureMarginDown() const;
	std::pair<const char *, uint8_t> getTimeNow() const;
	int16_t getAverageTemperature() const;

	unsigned long inline getMaxSampleAgeMs() const {
		return 3 * 60 * 1000; // 3 mins
	}

public:
	bool debugLog_ = true;
	bool enabled_ = false;
	std::string name_; // TODO memory limit to 15 to avoid allocation?
	BleAddress_t sensorAddress_;
	std::vector<uint8_t> valves_;
	int baseTemperature_ = 2100; // TODO uint16_t
	int temperatureMarginUp_ = 20; // TODO int8_t
	int temperatureMarginDown_ = 20; // TODO int8_t
	std::vector<TemperatureSetting> temperatures_;
	std::unique_ptr<TemporaryOverride> temporaryOverride_; // TODO use optional? why?
private:
	CircularBuffer<temperatureData_t, 10> temperatureData_;
	SemaphoreHandle_t mutex_ = xSemaphoreCreateMutex();

//	// statistical data
	int8_t batteryLevel_ = -1;
	int16_t currentHumidity_ = std::numeric_limits<decltype(currentHumidity_)>::min(); // humidity 6005 - 60.05%
};
}
