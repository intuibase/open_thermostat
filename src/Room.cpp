#include "Logging.h"


#include "Room.h"
#include <Arduino.h>
#include <limits>
#include <atomic>
#ifdef ARDUINO
#include <esp_system.h>
#endif

namespace heating {
namespace {
uint32_t diagnosticBootId() {
#ifdef ARDUINO
	static const uint32_t id = esp_random();
#else
	static const uint32_t id = static_cast<uint32_t>(Room::clock_t::now().time_since_epoch().count());
#endif
	return id;
}
std::atomic<uint32_t> diagnosticSequence{0};
}


void Room::storeBattery(int8_t batteryLevel) {
	batteryLevel_ = batteryLevel;
	DBGLOGFD(log_, logFeature_, "storeBattery %-15.15s batt: %d%%\n", config_.name_.c_str(), batteryLevel);
}

void Room::storeHumidity(int16_t humidity) {
	currentHumidity_ = humidity;
	DBGLOGFD(log_, logFeature_, "storeHumidity %-15.15s humidity: %d%%\n", config_.name_.c_str(), humidity);
}

void Room::storeTemperature(int16_t temperature) {
	std::lock_guard<std::mutex> lock(mutex_);
	DBGLOGFD(log_, logFeature_, "storeTemperature %-15.15s temp: %d\n", config_.name_.c_str(), temperature);
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
		DBGLOGFD(log_, logFeature_, "SSB  %-15.15s no samples\n", config_.name_.c_str());
		optimalStart_.update(config_.adaptiveStart_, wallClock_(), {}, thermalModel_, NAN, NAN, NAN, false, false);
		if (predictionCycle_.targetTime) logAdaptiveDiagnostic("cycle_cancelled", "missing_room_data");
		predictionCycle_ = {};
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
		DBGLOGFD(log_, logFeature_, "SSB  %-15.15s %d dOw: %d mean: unavailable, set: %d, margin: u%d/d%d Boiler: 0 Heat: 0 no samples\n", config_.name_.c_str(), time, dayOfTheWeek, currentSet, getTemperatureMarginUp(), getTemperatureMarginDown());
		optimalStart_.update(config_.adaptiveStart_, wallClock_(), {}, thermalModel_, NAN, NAN, NAN, false, false);
		if (predictionCycle_.targetTime) logAdaptiveDiagnostic("cycle_cancelled", "missing_room_data");
		predictionCycle_ = {};
		return std::make_tuple(Room::TemperatureStatus::MISSING_TEMPERATURE, std::nullopt);
	}

	updateOptimalStart(currentSet, *meanTemperature);
	effectiveSet_ = currentSet;

	bool shouldStartBoiler = meanTemperature < currentSet - getTemperatureMarginDown();
	bool shouldContinueHeating = meanTemperature < currentSet + getTemperatureMarginUp();

	stats.shouldHeat_ = shouldContinueHeating;
	stats.shouldStartBoiler_ = shouldStartBoiler;

	DBGLOGFD(log_, logFeature_, "SSB  %-15.15s %d mean: %d, set: %d, margin: u%d/d%d Override: %d left Boiler: %d Heat: %d. Boiler temp override: %d\n", config_.name_.c_str(), time, meanTemperature.value(), currentSet, getTemperatureMarginUp(), getTemperatureMarginDown(), (temporaryOverride_ && temporaryOverride_->isValid()) ? temporaryOverride_->getSecondsLeft() : 0, shouldStartBoiler, shouldContinueHeating, currentProgram ? currentProgram->getHeatingTemperatureOverride().value_or(0) : 0);

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
		DBGLOGFD(log_, logFeature_, "isTemperatureValid %-15.15s temperature too old, last read time: %ld, current: %ld\n", config_.name_.c_str(), lastSampleTime, millis());
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
	const std::time_t now = wallClock_();
	struct tm local{};
	if (!localtime_r(&now, &local)) return {0, 0};
	return {static_cast<uint16_t>(local.tm_hour * 100 + local.tm_min), static_cast<uint8_t>(local.tm_wday)};
}

std::optional<int16_t> Room::getAverageTemperature() const {
	int32_t avgTemp = 0;

	// TODO int64_t esp_timer_get_time()
	auto currentTime = clock_t::now();
	size_t validSamples = 0;

	//		DBGLOGFI(log_, logFeature_, "getMeanTemperature %-15.15s size: %zu\n", config_.name_.c_str(), temperatureData_.size());
	for (size_t i = 0; i < temperatureData_.size(); i++) {
		//			DBGLOGFI(log_, logFeature_, "%-15.15s idx: %d t: %f\n", config_.name_.c_str(), i, std::get<1>(temperatureData_.get(i)));
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

	DBGLOGFD(log_, logFeature_, "GAT  %-15.15s samples: %zu, valid: %zu, AvgTemp: %d (%f)\n", config_.name_.c_str(), temperatureData_.size(), validSamples, avgTemp, (float)avgTemp / 100);

	return avgTemp;
}

std::string Room::getStatus() const {
	std::stringstream ss;
	getStatus(ss);
	return ss.str();
}

void Room::getStatus(std::ostream &ss) const {
	getStatus(ss, false);
}

void Room::getStatus(std::ostream &ss, bool isBeingHeated) const {
	std::lock_guard<std::mutex> lock(mutex_);

	ss << "{\"name\": \"" << config_.name_ << "\", \"id\": \"" << config_.id_ << "\", \"enabled\": " << (config_.enabled_ ? "true" : "false");

	if (!temperatureData_.empty()) {
		auto [lastSampleTime, lastSampleTemp] = temperatureData_.newest();
		ss << ", \"currentTemp\": " << lastSampleTemp;
		ss << ", \"currentHumidity\": " << currentHumidity_.load();
		ss << ", \"currentTempAgeMs\": " << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - lastSampleTime).count();
		auto batteryLevel = batteryLevel_.load();
		if (batteryLevel >= 0) {
			ss << ", \"batteryLevel\": " << static_cast<int>(batteryLevel);
		}
		ss << ", \"meanTemp\": " << getAverageTemperature().value_or(0);
	}

	auto temperatureSet = effectiveSet_ ? effectiveSet_ : (stats.currentProgram_ ? stats.currentProgram_->temperature_ : config_.baseTemperature_);

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
	ss << ", \"enabledState\": \"" << (config_.enabled_ ? "on" : "off") << "\"";
	ss << ", \"isBeingHeatedState\": \"" << (isBeingHeated ? "on" : "off") << "\"";
	ss << ", \"isBeingHeated\": " << (isBeingHeated ? "true" : "false");
	ss << ", \"valves\": [";

	bool firstValve = true;

	auto const &valves = stats.currentProgram_ && !stats.currentProgram_->valves_.empty() ? stats.currentProgram_->valves_ : config_.valves_;
	for (auto const &valve : valves) {
		if (firstValve) {
			ss << "\"" << valve << "\"";
			firstValve = false;
		} else {
			ss << ", \"" << valve << "\"";
		}
	}
	ss << "]";
	writeAdaptiveStatus(ss);
	ss << "}";
}




void Room::updateThermalContext(std::optional<float> outdoor, std::optional<float> actualSupply,
	std::optional<float> predictedSupply, bool deliveredHeat, bool trustworthy, aos::HeatContext context) {
	std::lock_guard<std::mutex> lock(mutex_);
	heatContext_ = context; deliveredHeat_ = deliveredHeat; trustworthyHeat_ = trustworthy;
	outdoor_ = outdoor.value_or(NAN);
	actualSupply_ = actualSupply.value_or(NAN);
	predictedSupply_ = predictedSupply.value_or(NAN);
	if (!config_.enabled_ || !config_.adaptiveStart_.thermalLearningEnabled) return;
	const auto room = getAverageTemperature();
	const std::time_t wall = wallClock_();
	struct tm local{};
	localtime_r(&wall, &local);
	const auto scheduled = getTemperatureSet(local.tm_hour * 100 + local.tm_min, local.tm_wday);
	const bool roomValid = room && isTemperatureValid() && *room >= -1000 && *room <= 5000;
	const bool confirmedHeat = deliveredHeat && trustworthy && roomValid &&
		std::isfinite(actualSupply_) && actualSupply_ > *room / 100.0f + 5;
	// EMS burner cycling is a normal pause, even while heat is requested.
	const bool ordinaryPause = !deliveredHeat && heatContext_.usesEms &&
		heatContext_.emsHeating == false && !heatContext_.manualTest;
	const bool contextValid = (trustworthy || ordinaryPause) && (!deliveredHeat || confirmedHeat);
	warmupEligible_ = roomValid && local.tm_year >= 120 && contextValid &&
		!heatContext_.manualTest && !(temporaryOverride_ && temporaryOverride_->isValid()) &&
		!optimalStart_.decision.apply && !heatContext_.supplyBoostApplied && !heatContext_.supplyBaselineUnknown;
	confirmedHeat_ = confirmedHeat;
	// Disturbances must survive the five-minute sampling gate.
	if (!warmupEligible_) { thermalModel_.cancelWarmup(); warmupResetPending_ = true; }
	if (!roomValid || !contextValid || heatContext_.manualTest) predictionCycle_.clean = false;
	if (thermalContextSeen_ && deliveredHeat != lastDeliveredHeat_) {
		thermalIntervalValid_ = false; intervalReason_ = "heat_state_changed";
	}
	if (!trustworthy) intervalReason_ = heatContext_.invalidReason;
	thermalIntervalValid_ = thermalIntervalValid_ && trustworthy;
	thermalContextSeen_ = true;
	lastDeliveredHeat_ = deliveredHeat;
	if (!thermalHistory_) thermalHistory_ = std::make_unique<aos::ThermalHistory>();
	const auto now = std::chrono::duration_cast<std::chrono::seconds>(clock_t::now().time_since_epoch()).count();
	aos::Sample sample{now, room ? *room / 100.0f : NAN, outdoor_, actualSupply_, deliveredHeat,
		thermalIntervalValid_ && room.has_value() && *room >= -1000 && *room <= 5000 && isTemperatureValid() &&
		(!deliveredHeat || !actualSupply || *actualSupply > *room / 100.0f + 5)};
	if (thermalHistory_->push(sample)) {
		sampleReason_ = !room ? (temperatureData_.empty() ? "missing_room" : "stale_room") : !isTemperatureValid() ? "stale_room" :
			(*room < -1000 || *room > 5000) ? "room_out_of_range" :
			!thermalIntervalValid_ ? intervalReason_ :
			(deliveredHeat && actualSupply && *actualSupply <= *room / 100.0f + 5) ? "supply_not_hot" : "ok";
		thermalIntervalValid_ = true; intervalReason_ = "ok";
		thermalModel_.observe(*thermalHistory_);
		thermalModel_.observeWarmup(now, sample.room, scheduled.first / 100.0f, outdoor_, predictedSupply_,
			warmupEligible_, confirmedHeat);
		if (thermalHistory_->size() > 1 && std::abs(sample.room - thermalHistory_->at(1).room) > 0.6f) predictionCycle_.clean = false;
		logAdaptiveDiagnostic("sample", thermalModel_.observation.reason);
		warmupResetPending_ = false;
	}
	// At most one flash write per six hours, only after model/statistics changed.
	if (thermalModel_.dirty && now - lastPersist_ >= 6 * 3600) {
		lastPersist_ = now;
		const bool saved = aos::saveModel(config_.id_, config_.sensorAddress_, thermalModel_.state);
		if (saved) thermalModel_.dirty = false;
		logAdaptiveDiagnostic("persist", saved ? "saved" : "save_failed");
	}
}

void Room::updateOptimalStart(int16_t &set, int16_t roomTemperature) {
	const int64_t now = wallClock_();
	const auto monotonic = std::chrono::duration_cast<std::chrono::seconds>(clock_t::now().time_since_epoch()).count();
	const std::time_t wall = now;
	struct tm local{};
	localtime_r(&wall, &local);
	struct tm utc{};
	gmtime_r(&wall, &utc);
	int dayOffset = local.tm_yday - utc.tm_yday;
	if (dayOffset > 1) dayOffset = -1;
	if (dayOffset < -1) dayOffset = 1;
	const long utcOffset = dayOffset * 86400L + (local.tm_hour - utc.tm_hour) * 3600L + (local.tm_min - utc.tm_min) * 60L;
	const bool validTime = local.tm_year >= 120;
	const bool overrideActive = temporaryOverride_ && temporaryOverride_->isValid();
	if (lastWallTime_ && (std::abs((now - lastWallTime_) - (monotonic - lastMonotonic_)) > 90 || utcOffset != lastUtcOffset_)) {
		logAdaptiveDiagnostic("clock_changed", "wall_clock_or_timezone");
		optimalStart_.reset(); nextEvent_ = {}; lastScheduleCheck_ = 0; predictionCycle_ = {};
		thermalModel_.cancelWarmup();
	}
	lastWallTime_ = now; lastMonotonic_ = monotonic; lastUtcOffset_ = utcOffset;
	if (overrideActive && predictionCycle_.targetTime) {
		logAdaptiveDiagnostic("cycle_cancelled", "temporary_override"); predictionCycle_ = {};
	}
	if (predictionCycle_.targetTime && now >= predictionCycle_.targetTime && set < predictionCycle_.target) {
		logAdaptiveDiagnostic("cycle_cancelled", "schedule_decreased"); predictionCycle_ = {};
	}
	if (predictionCycle_.targetTime) {
		if (!predictionCycle_.reached && roomTemperature >= predictionCycle_.target - 5) {
			predictionCycle_.reached = now; logAdaptiveDiagnostic("target_reached");
		}
		if (now >= predictionCycle_.targetTime && predictionCycle_.reached) {
			if (predictionCycle_.clean && config_.adaptiveStart_.thermalLearningEnabled) {
				thermalModel_.recordError((*predictionCycle_.reached - predictionCycle_.targetTime) / 60.0f,
					(*predictionCycle_.reached - predictionCycle_.startTime) / 60.0f - predictionCycle_.predictedMinutes);
			}
			logAdaptiveDiagnostic("cycle_completed", !predictionCycle_.clean ? "disturbed" :
				config_.adaptiveStart_.thermalLearningEnabled ? "metrics_recorded" : "learning_disabled");
			predictionCycle_ = {};
		} else if (now > predictionCycle_.targetTime + 12 * 3600) {
			logAdaptiveDiagnostic("cycle_cancelled", "target_timeout"); predictionCycle_ = {};
		}
	}
	if (config_.adaptiveStart_.optimalStartEnabled && validTime &&
		(!lastScheduleCheck_ || monotonic - lastScheduleCheck_ >= 180 || (nextEvent_.targetTime && nextEvent_.targetTime <= now))) {
		lastScheduleCheck_ = monotonic;
		nextEvent_ = aos::nextIncrease(now, [this](int64_t instant) {
			const std::time_t t = instant;
			struct tm local{};
			localtime_r(&t, &local);
			return getTemperatureSet(local.tm_hour * 100 + local.tm_min, local.tm_wday).first;
		});
	}
	optimalStart_.update(config_.adaptiveStart_, now, nextEvent_, thermalModel_, roomTemperature / 100.0f,
		outdoor_, predictedSupply_, overrideActive, validTime && config_.enabled_ && roomTemperature >= -1000 && roomTemperature <= 5000);
	const auto &decision = optimalStart_.decision;

	if (decision.apply) {
		set = std::max(set, decision.event.temperature);
		if (!predictionCycle_.targetTime) {
			predictionCycle_ = {decision.event.targetTime, now, decision.event.temperature, decision.prediction.minutes, true, {}};
			logAdaptiveDiagnostic("cycle_started");
		}
	}
}

bool Room::resetThermalModel() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!aos::eraseModel(config_.id_)) { logAdaptiveDiagnostic("reset", "erase_failed"); return false; }
	thermalModel_ = {}; thermalHistory_.reset(); optimalStart_.reset(); predictionCycle_ = {};
	lastScheduleCheck_ = 0;
	thermalContextSeen_ = false; thermalIntervalValid_ = true;
	modelLoaded_ = false; sampleReason_ = "not_sampled";
	logAdaptiveDiagnostic("reset", "model_reset");
	return true;
}

void Room::inheritThermalState(Room &previous) {
	std::scoped_lock lock(mutex_, previous.mutex_);
	if (config_.sensorAddress_ != previous.config_.sensorAddress_) return;
	thermalModel_ = previous.thermalModel_;
	thermalModel_.cancelWarmup();
	if (config_.enabled_ && config_.adaptiveStart_.thermalLearningEnabled)
		thermalHistory_ = std::move(previous.thermalHistory_);
	else {
		thermalHistory_.reset();
		previous.thermalHistory_.reset();
	}
	thermalContextSeen_ = false; thermalIntervalValid_ = false;
	temperatureData_ = previous.temperatureData_;
	temporaryOverride_ = std::move(previous.temporaryOverride_);
	lastPersist_ = previous.lastPersist_;
	modelLoaded_ = previous.modelLoaded_;
	logAdaptiveDiagnostic("reload", "same_sensor_state_preserved");
}

void Room::writeAdaptiveStatus(std::ostream &ss) const {
	const auto &d = optimalStart_.decision;
	const auto &m = thermalModel_.state;
	ss << ", \"adaptiveStart\": {\"status\": \"" << aos::OptimalStartScheduler::name(d.status)
		<< "\", \"learningEnabled\": " << (config_.adaptiveStart_.thermalLearningEnabled ? "true" : "false")
		<< ", \"enabled\": " << (config_.adaptiveStart_.optimalStartEnabled ? "true" : "false")
		<< ", \"shadowMode\": " << (config_.adaptiveStart_.shadowMode ? "true" : "false")
		<< ", \"modelStatus\": \"" << (thermalModel_.calibrated() ? "Calibrated" : thermalModel_.ready() ? "Adaptive" : (m.fallbackWindows >= 12 ? "Fallback" : "Learning")) << "\""
		<< ", \"confidence\": " << thermalModel_.confidence()
		<< ", \"historySamples\": " << (thermalHistory_ ? thermalHistory_->size() : 0)
		<< ", \"heatingWindows\": " << m.heatingWindows << ", \"coolingWindows\": " << m.coolingWindows
		<< ", \"fallbackWindows\": " << m.fallbackWindows
		<< ", \"warmupWindows\": " << m.warmupWindows << ", \"warmupRate\": " << m.warmupRate
		<< ", \"calibrated\": " << (d.prediction.calibrated ? "true" : "false")
		<< ", \"targetTime\": " << d.event.targetTime << ", \"targetTemp\": " << d.event.temperature
		<< ", \"startTime\": " << d.startTime << ", \"predictedMinutes\": " << d.prediction.minutes
		<< ", \"delayMinutes\": " << m.delayMinutes << ", \"marginMinutes\": " << config_.adaptiveStart_.marginMinutes
		<< ", \"limited\": " << (d.limited ? "true" : "false")
		<< ", \"fallback\": " << (d.prediction.fallback ? "true" : "false")
		<< ", \"unreachable\": " << (d.prediction.unreachable ? "true" : "false")
		<< ", \"predictions\": " << m.predictions << ", \"meanErrorMinutes\": " << m.meanError
		<< ", \"meanDurationErrorMinutes\": " << m.meanDurationError
		<< ", \"durationMaeMinutes\": " << m.durationAbsoluteError
		<< ", \"earlyHoldingMinutes\": " << m.earlyHoldingMinutes
		<< ", \"maeMinutes\": " << m.absoluteError << ", \"lastErrorMinutes\": " << m.lastError;
	if (std::isfinite(outdoor_)) ss << ", \"outdoorTemp\": " << outdoor_;
	if (std::isfinite(actualSupply_)) ss << ", \"actualSupplyTemp\": " << actualSupply_;
	if (std::isfinite(predictedSupply_)) ss << ", \"predictedSupplyTemp\": " << predictedSupply_;
	ss << "}";
}

void Room::logAdaptiveSnapshot() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!log_ || !log_->doesFeatureMeetsLevelCondition(ib::logger::LoggerInterface::LogLevel::INFO, aosLogFeature_)) return;
	const auto now = std::chrono::duration_cast<std::chrono::seconds>(clock_t::now().time_since_epoch()).count();
	const auto &d = optimalStart_.decision;
	if (!lastLoggedStatus_ || *lastLoggedStatus_ != d.status || lastLoggedTarget_ != d.event.targetTime) {
		logAdaptiveDiagnostic("decision"); diagnosticCadence_.mark(now);
	} else if (diagnosticCadence_.due(now)) {
		logAdaptiveDiagnostic("snapshot"); diagnosticCadence_.mark(now);
	}
	lastLoggedStatus_ = d.status; lastLoggedTarget_ = d.event.targetTime;
}

void Room::logAdaptiveDiagnostic(const char *event, const char *reason) const {
	if (!log_ || !log_->doesFeatureMeetsLevelCondition(ib::logger::LoggerInterface::LogLevel::INFO, aosLogFeature_)) return;
	const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(clock_t::now().time_since_epoch()).count();
	const auto wall = wallClock_();
	struct tm local{}; localtime_r(&wall, &local);
	const auto mean = getAverageTemperature();
	const auto scheduled = getTemperatureSet(local.tm_hour * 100 + local.tm_min, local.tm_wday).first;
	const bool overridden = temporaryOverride_ && temporaryOverride_->isValid();
	const auto &d = optimalStart_.decision;
	const auto &m = thermalModel_.state;
	const auto &o = thermalModel_.observation;
	std::ostringstream out;
	out.precision(7);
	aos::DiagnosticJson j(out);
	j.number("v", 1); j.text("build", __DATE__ " " __TIME__); j.number("boot", diagnosticBootId()); j.number("seq", ++diagnosticSequence);
	j.text("event", event); j.text("reason", reason); j.text("program", heatContext_.program); j.text("room_id", config_.id_); j.text("room", config_.name_);
	if (local.tm_year >= 120) j.number("ts", wall); else j.null("ts");
	j.number("uptime_s", uptime); j.boolean("time_valid", local.tm_year >= 120);
	j.boolean("room_enabled", config_.enabled_); j.boolean("learning_enabled", config_.adaptiveStart_.thermalLearningEnabled);
	j.boolean("aos_enabled", config_.adaptiveStart_.optimalStartEnabled); j.boolean("shadow", config_.adaptiveStart_.shadowMode);
	j.number("room_c", mean ? *mean / 100.0f : NAN);
	if (!temperatureData_.empty()) {
		const auto &last = temperatureData_.newest();
		j.number("raw_room_c", std::get<1>(last) / 100.0f);
		j.number("room_age_s", std::chrono::duration_cast<std::chrono::seconds>(clock_t::now() - std::get<0>(last)).count());
	} else { j.null("raw_room_c"); j.null("room_age_s"); }
	j.boolean("room_fresh", isTemperatureValid() && mean.has_value());
	j.number("outdoor_c", outdoor_); j.text("outdoor_source", heatContext_.outdoorSource);
	j.number("actual_supply_c", actualSupply_); j.number("predicted_supply_c", predictedSupply_);
	j.boolean("boiler_commanded", heatContext_.boilerCommanded); j.boolean("valve_open", heatContext_.valveOpen);
	j.boolean("uses_ems", heatContext_.usesEms); j.boolean("ems_heating", heatContext_.emsHeating);
	j.optionalNumber("base_supply_c", heatContext_.baseSupply); j.optionalNumber("applied_supply_c", heatContext_.appliedSupply);
	j.boolean("supply_baseline_unknown", heatContext_.supplyBaselineUnknown);
	j.boolean("supply_boost_applied", heatContext_.supplyBoostApplied); j.text("supply_reason", heatContext_.supplyReason);
	j.boolean("manual_test", heatContext_.manualTest); j.boolean("heat_delivered", deliveredHeat_);
	j.boolean("warmup_eligible", warmupEligible_); j.boolean("confirmed_heat", confirmedHeat_);
	j.boolean("warmup_reset", warmupResetPending_);
	j.boolean("heat_trustworthy", trustworthyHeat_); j.text("heat_reason", trustworthyHeat_ ? "ok" : heatContext_.invalidReason);
	j.boolean("interval_valid", thermalIntervalValid_); j.text("interval_reason", intervalReason_);
	j.text("sample_reason", sampleReason_);
	if (thermalHistory_ && thermalHistory_->size()) {
		const auto &sample = thermalHistory_->newest();
		j.boolean("sample_valid", sample.valid); j.number("sample_uptime_s", sample.time);
	} else { j.null("sample_valid"); j.null("sample_uptime_s"); }
	j.number("base_set_c", config_.baseTemperature_ / 100.0f);
	j.number("hysteresis_up_c", config_.temperatureMarginUp_ / 100.0f); j.number("hysteresis_down_c", config_.temperatureMarginDown_ / 100.0f);
	j.boolean("room_demand", stats.shouldStartBoiler_); j.boolean("room_continue", stats.shouldHeat_);
	j.number("scheduled_set_c", scheduled / 100.0f);
	j.number("effective_set_c", (overridden ? temporaryOverride_->getTemperature() : d.apply ? std::max(scheduled, d.event.temperature) : scheduled) / 100.0f);
	j.number("override_s", overridden ? temporaryOverride_->getSecondsLeft() : 0);
	j.text("status", aos::OptimalStartScheduler::name(d.status)); j.boolean("apply", d.apply);
	j.number("target_ts", d.event.targetTime); j.number("previous_ts", d.event.previousTime);
	j.number("target_c", d.event.temperature / 100.0f); j.number("planned_start_ts", d.startTime);
	j.number("predicted_min", d.prediction.minutes); j.number("margin_min", config_.adaptiveStart_.marginMinutes);
	j.number("max_lead_min", config_.adaptiveStart_.maxLeadMinutes);
	j.boolean("fallback", d.prediction.fallback); j.boolean("limited", d.limited); j.boolean("unreachable", d.prediction.unreachable);
	j.number("gain", m.gain); j.number("loss", m.loss); j.number("delay_min", m.delayMinutes); j.number("fallback_c_per_h", m.fallbackRate);
	j.number("coverage_pct", thermalModel_.confidence()); j.boolean("model_ready", thermalModel_.ready());
	j.boolean("model_loaded", modelLoaded_); j.boolean("model_dirty", thermalModel_.dirty);
	j.number("history_samples", thermalHistory_ ? thermalHistory_->size() : 0);
	j.number("heating_windows", m.heatingWindows); j.number("cooling_windows", m.coolingWindows); j.number("fallback_windows", m.fallbackWindows);
	j.number("warmup_windows", m.warmupWindows); j.number("warmup_c_per_h", m.warmupRate);
	j.boolean("calibrated", d.prediction.calibrated);
	j.number("delay_cycles", m.delayCycles); j.text("observation", config_.adaptiveStart_.thermalLearningEnabled ? o.reason : "learning_disabled");
	j.boolean("window_checked", o.windowChecked); j.boolean("window_accepted", o.accepted); j.boolean("delay_updated", o.delayUpdated);
	j.number("window_start_uptime_s", o.windowStart); j.number("window_end_uptime_s", o.windowEnd);
	j.number("rate_c_per_h", o.rate); j.number("emission", o.emission); j.number("room_outdoor_delta_c", o.roomOutdoorDelta);
	j.number("cycle_target_ts", predictionCycle_.targetTime); j.number("cycle_start_ts", predictionCycle_.startTime);
	j.number("cycle_target_c", predictionCycle_.target / 100.0f); j.number("cycle_predicted_min", predictionCycle_.predictedMinutes);
	j.boolean("cycle_clean", predictionCycle_.clean); j.optionalNumber("reached_ts", predictionCycle_.reached);
	j.number("observed_cycles", m.predictions); j.number("mean_error_min", m.meanError); j.number("mae_min", m.absoluteError);
	j.number("last_error_min", m.lastError); j.number("mean_duration_error_min", m.meanDurationError);
	j.number("duration_mae_min", m.durationAbsoluteError); j.number("early_holding_min", m.earlyHoldingMinutes);
	j.finish();
	const auto record = out.str();
	DBGLOGFI(log_, aosLogFeature_, "AOS_DIAG %s\n", record.c_str());
}

}
