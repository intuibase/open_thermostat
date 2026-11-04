#pragma once

#include "RoomConfig.h"
#include "BeaconBleAddress.h"
#include "CircularBuffer.h"
#include "Logging.h"
#include "ThermalPersistence.h"
#include "AosDiagnostics.h"

#include <atomic>
#include <functional>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <time.h>
#include <mutex>
#include <tuple>
#include <optional>

namespace heating {
class Room {
public:
	using clock_t = std::chrono::steady_clock;

	enum class TemperatureStatus : uint8_t { MISSING_TEMPERATURE, TEMPERATURE_OK, START_HEATING, CONTINUE_HEATING };

	Room(std::shared_ptr<ib::logger::LoggerInterface> log, RoomConfig config, std::function<std::time_t()> wallClock = [] { return std::time(nullptr); })
		: log_(std::move(log)), wallClock_(std::move(wallClock)), config_(std::move(config)) {
		modelLoaded_ = aos::loadModel(config_.id_, config_.sensorAddress_, thermalModel_.state);
		if (log_) {
			logFeature_ = log_->addFeature("Room");
			aosLogFeature_ = log_->addFeature("AOS");
		}
		logAdaptiveDiagnostic("config", modelLoaded_ ? "model_restored" : "default_model");
	}

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
	void updateThermalContext(std::optional<float> outdoor, std::optional<float> actualSupply,
		std::optional<float> predictedSupply, bool deliveredHeat, bool trustworthy, aos::HeatContext context = {});
	void logAdaptiveSnapshot();
	bool resetThermalModel();
	void inheritThermalState(Room &previous);


	const std::string &getName() const { return config_.name_; }
	const std::string &getId() const { return config_.id_; }

	auto getValves() const { return config_.valves_; }
	auto const &getSensorAddress() const { return config_.sensorAddress_; }

	std::string getStatus() const;
	void getStatus(std::ostream &ss) const;
	void getStatus(std::ostream &ss, bool isBeingHeated) const;


private:
	friend struct RoomTestPeer;
	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_{}, aosLogFeature_{};
	bool isTemperatureValid() const;
	std::pair<int16_t, const RoomConfig::TemperatureSetting *> getTemperatureSet(uint16_t currentTime, uint8_t dayOfTheWeek) const; // HH:MM 	// returns temperature set for current time - maximum one from all, but always overrides base temp
	int16_t getTemperatureMarginUp() const;
	int16_t getTemperatureMarginDown() const;
	std::pair<uint16_t, uint8_t> getTimeNow() const;
	std::optional<int16_t> getAverageTemperature() const;

	auto getMaxSampleAgeMs() const { return std::chrono::minutes(3); }

	std::function<std::time_t()> wallClock_;
	RoomConfig config_;
	bool modelLoaded_ = false;
	aos::HeatContext heatContext_;
	bool deliveredHeat_ = false, trustworthyHeat_ = false;
	bool warmupEligible_ = false, confirmedHeat_ = false, warmupResetPending_ = false;
	const char *intervalReason_ = "ok", *sampleReason_ = "not_sampled";
	aos::DiagnosticCadence diagnosticCadence_;
	std::optional<aos::OptimalStartScheduler::Status> lastLoggedStatus_;
	int64_t lastLoggedTarget_ = 0;
	void logAdaptiveDiagnostic(const char *event, const char *reason = "ok") const;
	aos::ThermalModel thermalModel_;
	std::unique_ptr<aos::ThermalHistory> thermalHistory_;
	aos::OptimalStartScheduler optimalStart_;
	aos::Event nextEvent_;
	bool thermalContextSeen_ = false, lastDeliveredHeat_ = false, thermalIntervalValid_ = true;
	float outdoor_ = NAN, actualSupply_ = NAN, predictedSupply_ = NAN;
	int64_t lastScheduleCheck_ = 0, lastWallTime_ = 0, lastMonotonic_ = 0, lastPersist_ = 0;
	long lastUtcOffset_ = 0;
	int16_t effectiveSet_ = 0;
	struct PredictionCycle {
		int64_t targetTime = 0, startTime = 0;
		int16_t target = 0;
		int predictedMinutes = 0;
		bool clean = true;
		std::optional<int64_t> reached;
	} predictionCycle_;
	void updateOptimalStart(int16_t &set, int16_t roomTemperature);
	void writeAdaptiveStatus(std::ostream &ss) const;

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
