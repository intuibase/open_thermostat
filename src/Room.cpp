

#include "Room.h"
#include <limits>

namespace heating {

void Room::storeBattery(int8_t batteryLevel) {
	batteryLevel_ = batteryLevel;
	DBGLOGROOM("storeBattery %-15.15s batt: %d%%\n", config_.name_.c_str(), batteryLevel);
}

void Room::storeHumidity(int16_t humidity) {
	currentHumidity_ = humidity;
	DBGLOGROOM("storeHumidity %-15.15s humidity: %d%%\n", config_.name_.c_str(), humidity);
}

void Room::storeTemperature(int16_t temperature) {
	std::lock_guard<std::mutex> lock(mutex_);
	DBGLOGROOM("storeTemperature %-15.15s temp: %d\n", config_.name_.c_str(), temperature);
	temperatureData_.push(temperatureData_t{clock_t::now(), temperature});
}

void Room::createTemporaryOverride(int16_t temperature, uint32_t validSeconds) {
	std::lock_guard<std::mutex> lock(mutex_);
	temporaryOverride_ = std::make_unique<TemporaryOverride>(temperature, validSeconds);
}

// optional boiler heating temperature override
std::tuple<Room::TemperatureStatus, std::optional<uint8_t>> Room::shouldStartBoilerAndHeat() {
	std::lock_guard<std::mutex> lock(mutex_);

	// start heat boiler, continue heat, data error
	if (!isTemperatureValid()) {
		DBGLOGROOM("SSB  %-15.15s no samples\n", config_.name_.c_str());
		return std::make_tuple(Room::TemperatureStatus::MISSING_TEMPERATURE, std::nullopt);
	}

	auto [time, dayOfTheWeek] = getTimeNow();

	auto [currentSet, currentProgram] = getTemperatureSet(time, dayOfTheWeek);
	stats.currentProgram_ = currentProgram;

	if (temporaryOverride_ && temporaryOverride_->isValid()) {
		currentSet = temporaryOverride_->getTemperature();
	}

	auto meanTemperature = getAverageTemperature();
	if (!meanTemperature.has_value()) {
		DBGLOGROOM("SSB  %-15.15s %d dOw: %d mean: %d, set: %d, margin: u%d/d%d Boiler: 0 Heat: 1 no samples\n", config_.name_.c_str(), time, dayOfTheWeek, meanTemperature, currentSet, getTemperatureMarginUp(), getTemperatureMarginDown());
		return std::make_tuple(Room::TemperatureStatus::MISSING_TEMPERATURE, std::nullopt);
	}

	bool shouldStartBoiler = meanTemperature < currentSet - getTemperatureMarginDown();
	bool shouldContinueHeating = meanTemperature < currentSet + getTemperatureMarginUp();

	stats.shouldHeat_ = shouldContinueHeating;
	stats.shouldStartBoiler_ = shouldStartBoiler;

	DBGLOGROOM("SSB  %-15.15s %d mean: %d, set: %d, margin: u%d/d%d Override: %d left Boiler: %d Heat: %d. Boiler temp override: %d\n", config_.name_.c_str(), time, meanTemperature, currentSet, getTemperatureMarginUp(), getTemperatureMarginDown(), (temporaryOverride_ && temporaryOverride_->isValid()) ? temporaryOverride_->getSecondsLeft() : 0, shouldStartBoiler, shouldContinueHeating, currentProgram ? currentProgram->getHeatingTemperatureOverride().value_or(0) : 0);

	Room::TemperatureStatus status{Room::TemperatureStatus::TEMPERATURE_OK};
	if (shouldStartBoiler) {
		status = Room::TemperatureStatus::START_HEATING;
	} else if (shouldContinueHeating) {
		status = Room::TemperatureStatus::CONTINUE_HEATING;
	}

	return std::make_tuple(status, currentProgram ? currentProgram->getHeatingTemperatureOverride() : std::optional<uint8_t>{});
}

bool Room::isEnabled() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return config_.enabled_;
}

bool Room::isTemperatureValid() const {
	if (temperatureData_.empty()) {
		return false;
	}

	auto lastSampleTime = std::get<0>(temperatureData_.newest());
	if (lastSampleTime + std::chrono::minutes(5) < clock_t::now()) {
		DBGLOGROOM("isTemperatureValid %-15.15s temperature too old, last read time: %ld, current: %ld\n", config_.name_.c_str(), lastSampleTime, millis());
		return false;
	}
	return true;
}

// returns temperature set for current time - maximum one from all, but always overrides base temp
std::pair<int16_t, const RoomConfig::TemperatureSetting *> Room::getTemperatureSet(uint16_t currentTime, uint8_t dayOfTheWeek) const { // HH:MM
	if (config_.temperatures_.empty()) {
		return {config_.baseTemperature_, nullptr};
	}

	int16_t value = std::numeric_limits<decltype(value)>::min();

	const RoomConfig::TemperatureSetting *program = nullptr;

	for (auto const &temp : config_.temperatures_) {
		if (temp.isEnabled() && temp.doesFit(currentTime, dayOfTheWeek)) {
			if (temp.getTemperature() > value) {
				value = temp.getTemperature();
				program = &temp;
			}
		}
	}

	if (value == std::numeric_limits<decltype(value)>::min()) {
		value = config_.baseTemperature_;
	}

	return {value, program};
}

int16_t Room::getTemperatureMarginUp() const {
	return config_.temperatureMarginUp_;
}

int16_t Room::getTemperatureMarginDown() const {
	return config_.temperatureMarginDown_;
}

std::pair<uint16_t, uint8_t> Room::getTimeNow() const {
	struct tm timeinfo;
	auto result = getLocalTime(&timeinfo);
	if (!result) {
		logger.printf("TIME ERROR\n");
	}
	uint16_t time = timeinfo.tm_hour * 100 + timeinfo.tm_min;
	return {time, timeinfo.tm_wday};
}

std::optional<int16_t> Room::getAverageTemperature() const {
	int16_t avgTemp = 0;

	// TODO int64_t esp_timer_get_time()
	auto currentTime = clock_t::now();
	size_t validSamples = 0;

	//		logger.printf("getMeanTemperature %-15.15s size: %zu\n", config_.name_.c_str(), temperatureData_.size());
	for (size_t i = 0; i < temperatureData_.size(); i++) {
		//			logger.printf("%-15.15s idx: %d t: %f\n", config_.name_.c_str(), i, std::get<1>(temperatureData_.get(i)));
		auto sampleTime = std::get<0>(temperatureData_.get(i));

		if (currentTime - sampleTime <= getMaxSampleAgeMs()) {
			validSamples++;
		} else {
			continue;
		}

		avgTemp += std::get<1>(temperatureData_.get(i));
	}

	if (validSamples == 0) {
		return std::nullopt;
	}

	avgTemp /= validSamples;

	DBGLOGROOM("GAT  %-15.15s samples: %zu, valid: %zu, AvgTemp: %d (%f)\n", config_.name_.c_str(), temperatureData_.size(), validSamples, avgTemp, (float)avgTemp / 100);

	return avgTemp;
}

std::string Room::getStatus() const {
	std::stringstream ss;
	getStatus(ss);
	return ss.str();
}

void Room::getStatus(std::ostream &ss) const {
	std::lock_guard<std::mutex> lock(mutex_);

	ss << "{\"name\": \"" << config_.name_ << "\", \"enabled\": " << (config_.enabled_ ? "true" : "false");

	if (!temperatureData_.empty()) {
		auto [lastSampleTime, lastSampleTemp] = temperatureData_.newest();
		ss << ", \"currentTemp\": " << lastSampleTemp;
		ss << ", \"currentHumidity\": " << currentHumidity_.load();
		ss << ", \"currentTempAgeMs\": " << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - lastSampleTime).count();
		ss << ", \"batteryLevel\": " << static_cast<int>(batteryLevel_.load());
		ss << ", \"meanTemp\": " << getAverageTemperature().value_or(0);
	}

	auto temperatureSet = (stats.currentProgram_ ? stats.currentProgram_->temperature_ : config_.baseTemperature_);

	std::string currentProgramName;

	uint32_t temporaryProgramSecondsLeft = 0;

	if (temporaryOverride_ && temporaryOverride_->isValid()) {
		temporaryProgramSecondsLeft = temporaryOverride_->getSecondsLeft();
		temperatureSet = temporaryOverride_->getTemperature();
	} else if (stats.currentProgram_) {
		currentProgramName = stats.currentProgram_->name_.c_str();
	}

	if (currentProgramName.empty()) {
		ss << ", \"currentProgram\": null";
	} else {
		ss << ", \"currentProgram\": \"" << currentProgramName << "\"";
	}
	ss << ", \"tempSet\": " << temperatureSet ;
	ss << ", \"tempMarginUp\": " << (int)config_.temperatureMarginUp_;
	ss << ", \"tempMarginDown\": " << (int)config_.temperatureMarginDown_;
	ss << ", \"temporaryProgramSecondsLeft\": " << temporaryProgramSecondsLeft;
	ss << ", \"shouldContinueHeating\": " << (stats.shouldHeat_ ? "true" : "false");
	ss << ", \"shouldStartBoiler\": " << (stats.shouldStartBoiler_ ? "true" : "false");
	ss << ", \"valves\": [";

	bool firstValve = true;

	auto const &valves = stats.currentProgram_ && !stats.currentProgram_->valves_.empty() ? stats.currentProgram_->valves_ : config_.valves_;
	for (auto valve : valves) {
		if (firstValve) {
			ss << (int)valve;
			firstValve = false;
		} else {
			ss << ", " << (int)valve;
		}
	}
	ss << "]}";
}




}
