#include <gtest/gtest.h>
#include "OptimalStart.h"
#include "ThermalPersistence.h"
#include "AdaptiveStartConfig.h"
#include "Room.h"
#include <cstdlib>
#include <sstream>

using namespace heating::aos;

TEST(ThermalHistory, BoundedCadenceAndChronology) {
	ThermalHistory history;
	for (int i = 0; i < 200; ++i) {
		ASSERT_TRUE(history.push({i * 300, 20, 0, 50, true, true}));
		EXPECT_FALSE(history.push({i * 300 + 1, 20, 0, 50, true, true}));
	}
	EXPECT_EQ(history.size(), 144u);
	EXPECT_EQ(history.newest().time, 199 * 300);
	EXPECT_EQ(history.at(143).time, 56 * 300);
	EXPECT_LE(sizeof(ThermalHistory) * 8, 30 * 1024u);
}

TEST(ThermalModel, FallbackAndImpossibleTarget) {
	ThermalModel model;
	EXPECT_TRUE(model.predict(18, 21, NAN, NAN).fallback);
	EXPECT_EQ(model.predict(18, 21, NAN, NAN).minutes, 190);
	model.state.heatingWindows = 12;
	model.state.coolingWindows = 6;
	EXPECT_TRUE(model.predict(18, 21, -20, 25).unreachable);
	EXPECT_TRUE(model.predict(NAN, 21, 0, 50).unreachable);
	EXPECT_EQ(model.predict(22, 21, 0, 50).minutes, 0);
}

TEST(ThermalModel, OutdoorSupplyAndRoomSpeedAffectPrediction) {
	ThermalModel model;
	model.state.heatingWindows = 12;
	model.state.coolingWindows = 6;
	const auto warm = model.predict(18, 21, 10, 55);
	const auto cold = model.predict(18, 21, -20, 55);
	EXPECT_LT(warm.minutes, cold.minutes);
	EXPECT_LT(model.predict(18, 21, -20, 65).minutes, cold.minutes);
	model.state.gain *= 2;
	EXPECT_LT(model.predict(18, 21, -20, 55).minutes, cold.minutes);
}

TEST(ThermalModel, RejectsDropoutsAndVentilation) {
	for (bool dropout : {false, true}) {
		ThermalHistory history;
		ThermalModel model;
		for (int i = 0; i < 20; ++i) {
			history.push({i * 300, i % 2 ? 17.0f : 20.0f, 0, 55, true, !dropout});
			model.observe(history);
		}
		EXPECT_EQ(model.state.heatingWindows, 0u);
		EXPECT_EQ(model.state.fallbackWindows, 0u);
	}
}

TEST(ThermalModel, LearnsOnOffWithoutOutdoorOrSupply) {
	ThermalHistory history;
	ThermalModel model;
	for (int i = 0; i < 30; ++i) {
		history.push({i * 300, 15 + i * 0.15f, NAN, NAN, true, true});
		model.observe(history);
	}
	EXPECT_GT(model.state.fallbackWindows, 0u);
	EXPECT_EQ(model.state.heatingWindows, 0u);
	EXPECT_GT(model.state.fallbackRate, 1.0f);
	EXPECT_TRUE(model.predict(18, 21, NAN, NAN).fallback);
}

TEST(ThermalModel, LearnsDeliveredHeatThatOnlyOffsetsLosses) {
	for (float change : {0.0f, -0.005f}) {
		ThermalHistory history;
		ThermalModel model;
		for (int i = 0; i < 37; ++i) {
			history.push({i * 300LL, 20 + change * i, 10, 42, true, true});
			model.observe(history);
		}
		EXPECT_GT(model.state.heatingWindows, 0u);
		EXPECT_LT(model.state.gain, 3);
		EXPECT_LT(model.state.fallbackRate, 1);
		EXPECT_TRUE(validState(model.state));
	}
}

TEST(ThermalModel, FlatHeatingWithoutTelemetryOnlyUpdatesFallback) {
	ThermalHistory history;
	ThermalModel model;
	for (int i = 0; i < 144; ++i) {
		history.push({i * 300LL, 20, NAN, NAN, true, true});
		model.observe(history);
	}
	EXPECT_EQ(model.state.heatingWindows, 0u);
	EXPECT_GT(model.state.fallbackWindows, 0u);
	EXPECT_GE(model.state.fallbackRate, 0.1f);
	EXPECT_LT(model.state.fallbackRate, 0.2f);
	EXPECT_TRUE(model.predict(20, 21, NAN, NAN).fallback);
}

static void scheduledWarmup(ThermalModel &model, int64_t base, bool valid = true, bool delivered = true) {
	model.cancelWarmup(); // Separate fixture episodes; production samples stay continuous overnight.
	model.observeWarmup(base, 20, 20, 5, 45, true, false);
	model.observeWarmup(base + 300, 20, 21, 5, 45, true, delivered);
	for (int i = 1; i <= 36; ++i)
		model.observeWarmup(base + (i + 1) * 300, 20 + i / 36.0f, 21, 5, 45, valid, delivered && i % 5 != 0);
}

TEST(ThermalModel, CalibratesFromScheduledWarmupsWithBoilerCycling) {
	ThermalModel model;
	for (int cycle = 0; cycle < 3; ++cycle) scheduledWarmup(model, cycle * 12 * 3600LL);
	EXPECT_EQ(model.state.warmupWindows, 3u);
	EXPECT_TRUE(model.calibrated());
	EXPECT_TRUE(validState(model.state));
	const auto prediction = model.predict(20, 21, 5, 45);
	EXPECT_TRUE(prediction.calibrated);
	EXPECT_FALSE(prediction.unreachable);
	EXPECT_GE(prediction.minutes, 140);
	EXPECT_LE(prediction.minutes, 210);
	EXPECT_GT(model.predict(20, 21, 0, 45).minutes, prediction.minutes);
	EXPECT_LT(model.predict(20, 21, 5, 50).minutes, prediction.minutes);
	EXPECT_TRUE(model.predict(20, 21, -30, 30).unreachable);
	EXPECT_TRUE(model.predict(20, 21, NAN, NAN).calibrated);
}

TEST(ThermalModel, DisturbedOrUnheatedWarmupsDoNotCalibrate) {
	for (bool valid : {false, true}) {
		ThermalModel model;
		for (int cycle = 0; cycle < 4; ++cycle) scheduledWarmup(model, cycle * 12 * 3600LL, valid, !valid);
		EXPECT_EQ(model.state.warmupWindows, 0u);
	}
	ThermalModel model;
	model.observeWarmup(0, 20, 20, 5, 45, true, true);
	model.observeWarmup(300, 20, 21, 5, 45, true, true);
	model.cancelWarmup(); // override, clock/program/config change
	for (int i = 2; i <= 38; ++i) model.observeWarmup(i * 300, 20 + (i - 2) / 36.0f, 21, 5, 45, true, true);
	EXPECT_EQ(model.state.warmupWindows, 0u);
}

TEST(ThermalPersistence, MigratesLegacyParametersWithoutClaimingWarmupCalibration) {
	ModelState original;
	original.gain = 4.5f; original.loss = 0.04f; original.heatingWindows = 28;
	LegacySnapshot legacy;
	std::memcpy(legacy.state.data(), &original, legacy.state.size());
	legacy.checksum = checksum(legacy);
	ModelState restored;
	ASSERT_TRUE(migrateSnapshot(legacy, {}, restored));
	EXPECT_FLOAT_EQ(restored.gain, original.gain);
	EXPECT_EQ(restored.heatingWindows, original.heatingWindows);
	EXPECT_EQ(restored.warmupWindows, 0u);
	EXPECT_TRUE(validState(restored));
	EXPECT_FALSE(migrateSnapshot(legacy, {1, 2, 3, 4, 5, 6}, restored));
	legacy.state[0] ^= 1;
	EXPECT_FALSE(migrateSnapshot(legacy, {}, restored));
}

TEST(ThermalModel, MultiWeekSimulationConvergesAcrossWeatherAndHydraulics) {
	for (const float gain : {2.5f, 5.0f}) {
		ThermalHistory history;
		ThermalModel model;
		float room = 19;
		for (int minute = 0; minute <= 28 * 24 * 60; minute += 5) {
			const int day = minute / 1440;
			const float outdoor = day % 2 ? -20 : 10;
			const float supply = day % 2 ? 65 : 50;
			const bool heat = minute % 360 < 180;
			history.push({minute * 60LL, std::round(room * 10) / 10, outdoor, supply, heat, true});
			model.observe(history);
			for (int m = 0; m < 5; ++m)
				room += ((heat ? gain * ThermalModel::emission(supply, room) : 0) - 0.03f * (room - outdoor)) / 60;
		}
		EXPECT_TRUE(model.ready());
		EXPECT_NEAR(model.state.gain, gain, gain * 0.2);
		EXPECT_NEAR(model.state.loss, 0.03f, 0.006f);
		EXPECT_EQ(model.confidence(), 100u);
	}
}

TEST(ThermalModel, MetricsAndPersistedStateValidation) {
	ThermalModel model;
	model.recordError(-6);
	model.recordError(10);
	EXPECT_FLOAT_EQ(model.state.meanError, 2);
	EXPECT_FLOAT_EQ(model.state.absoluteError, 8);
	EXPECT_FLOAT_EQ(model.state.lastError, 10);
	Snapshot snapshot{};
	snapshot.state = model.state;
	snapshot.checksum = checksum(snapshot);
	EXPECT_TRUE(validSnapshot(snapshot, {}));
	snapshot.version = 3;
	snapshot.checksum = checksum(snapshot);
	EXPECT_FALSE(validSnapshot(snapshot, {}));
	snapshot.version = 2;
	snapshot.state.gain = NAN;
	snapshot.checksum = checksum(snapshot);
	EXPECT_FALSE(validSnapshot(snapshot, {}));
	snapshot.state = {};
	snapshot.checksum = checksum(snapshot);
	EXPECT_TRUE(validSnapshot(snapshot, {}));
	snapshot.state.loss = 0.1;
	EXPECT_FALSE(validSnapshot(snapshot, {}));
	snapshot.checksum = checksum(snapshot);
	EXPECT_TRUE(validSnapshot(snapshot, {}));
	EXPECT_FALSE(validSnapshot(snapshot, {1, 2, 3, 4, 5, 6}));
	snapshot.state.warmupRate = NAN;
	snapshot.checksum = checksum(snapshot);
	EXPECT_FALSE(validSnapshot(snapshot, {}));
}

TEST(OptimalStart, HonorsOverrideShadowAndLatch) {
	Config config;
	config.optimalStartEnabled = true;
	ThermalModel model;
	OptimalStartScheduler scheduler;
	const Event event{36000, 10000, 2100};
	scheduler.update(config, 25000, event, model, 18, 0, 55, false, true);
	EXPECT_EQ(scheduler.decision.status, OptimalStartScheduler::Status::SHADOW);
	EXPECT_FALSE(scheduler.decision.apply);
	config.shadowMode = false;
	scheduler.update(config, 25100, event, model, 20.9f, 0, 55, false, true);
	EXPECT_TRUE(scheduler.decision.apply);
	scheduler.update(config, 25200, event, model, 19, 0, 55, true, true);
	EXPECT_EQ(scheduler.decision.status, OptimalStartScheduler::Status::OVERRIDE);
	EXPECT_FALSE(scheduler.decision.apply);
	scheduler.update(config, 25300, event, model, 19, 0, 55, false, false);
	EXPECT_EQ(scheduler.decision.status, OptimalStartScheduler::Status::NO_DATA);
}

TEST(OptimalStart, PreviousBoundaryAndLeadLimit) {
	Config config;
	config.optimalStartEnabled = true;
	config.shadowMode = false;
	config.maxLeadMinutes = 60;
	ThermalModel model;
	OptimalStartScheduler scheduler;
	scheduler.update(config, 30000, {36000, 34500, 2100}, model, 15, -20, 30, false, true);
	EXPECT_EQ(scheduler.decision.startTime, 34500);
	EXPECT_TRUE(scheduler.decision.limited);
	EXPECT_FALSE(scheduler.decision.apply);
	scheduler.update(config, 34500, {36000, 34500, 2100}, model, 15, -20, 30, false, true);
	EXPECT_TRUE(scheduler.decision.apply);
	scheduler.update(config, 36000, {36000, 34500, 2100}, model, 15, -20, 30, false, true);
	EXPECT_FALSE(scheduler.decision.apply);
}

TEST(OptimalStart, ScheduleTransitionsAndRestartRecalculation) {
	auto resolve = [](int64_t t) -> int16_t { return t < 6 * 3600 ? 1800 : (t < 8 * 3600 ? 2100 : 1900); };
	const auto event = nextIncrease(3 * 3600, resolve);
	EXPECT_EQ(event.targetTime, 6 * 3600);
	EXPECT_EQ(event.temperature, 2100);
	Config config;
	config.optimalStartEnabled = true;
	config.shadowMode = false;
	ThermalModel model;
	OptimalStartScheduler restarted;
	restarted.update(config, 3 * 3600, event, model, 18, 0, 50, false, true);
	EXPECT_TRUE(restarted.decision.apply);
	EXPECT_EQ(nextIncrease(6 * 3600, resolve).targetTime, 0);
}

TEST(OptimalStart, EightRoomsRemainIndependent) {
	std::array<OptimalStartScheduler, 8> schedulers;
	Config config;
	config.optimalStartEnabled = true;
	config.shadowMode = false;
	ThermalModel model;
	for (size_t i = 0; i < schedulers.size(); ++i)
		schedulers[i].update(config, 25000, {36000, 0, 2100}, model, 18, 0, 55, i == 3, true);
	for (size_t i = 0; i < schedulers.size(); ++i)
		EXPECT_EQ(schedulers[i].decision.apply, i != 3);
}

TEST(AdaptiveConfig, BackwardCompatibilityAndValidation) {
	for (const auto *json : {"{}", "{\"adaptive_start\":{}}", "{\"adaptive_start\":{\"maxLeadMinutes\":720,\"shadowMode\":false}}"}) {
		auto root = cJSON_Parse(json);
		EXPECT_TRUE(validConfigJson(root));
		cJSON_Delete(root);
	}
	for (const auto *json : {"{\"adaptive_start\":null}", "{\"adaptive_start\":{\"maxLeadMinutes\":721}}", "{\"adaptive_start\":{\"maxLeadMinutes\":59}}", "{\"adaptive_start\":{\"shadowMode\":1}}", "{\"adaptive_start\":{\"marginMinutes\":-1}}", "{\"adaptive_start\":{\"marginMinutes\":1.5}}"}) {
		auto root = cJSON_Parse(json);
		EXPECT_FALSE(validConfigJson(root));
		cJSON_Delete(root);
	}
}

// Exercise the production Room class, including schedule, override and boiler
// temperature output, instead of duplicating its decision logic in test code.
static heating::RoomConfig roomConfig(bool shadow = false) {
	heating::RoomConfig config{};
	config.id_ = "aabbccdd";
	config.name_ = "Test";
	config.enabled_ = true;
	config.baseTemperature_ = 1800;
	config.adaptiveStart_.optimalStartEnabled = true;
	config.adaptiveStart_.shadowMode = shadow;
	const auto now = std::time(nullptr);
	struct tm local{};
	localtime_r(&now, &local);
	const int from = (local.tm_hour * 60 + local.tm_min + 60) % 1440;
	const int to = (from + 120) % 1440;
	heating::RoomConfig::TemperatureSetting setting;
	setting.timeFrom_ = from / 60 * 100 + from % 60;
	setting.timeTo_ = to / 60 * 100 + to % 60;
	setting.temperature_ = 2100;
	setting.heatingTemperatureOverride_ = 70;
	config.temperatures_.push_back(setting);
	return config;
}

TEST(AdaptiveRoom, PreheatsWithoutPromotingFutureBoilerOverride) {
	heating::Room room({}, roomConfig());
	room.storeTemperature(1900);
	room.updateThermalContext(0, 45, 50, false, true);
	const auto [status, boilerOverride] = room.shouldStartBoilerAndHeat();
	EXPECT_EQ(status, heating::Room::TemperatureStatus::START_HEATING);
	EXPECT_FALSE(boilerOverride.has_value());
	EXPECT_NE(room.getStatus().find("PREHEATING"), std::string::npos);
	EXPECT_NE(room.getStatus().find("\"tempSet\": 2100"), std::string::npos);
	room.createTemporaryOverride(1700, 600);
	EXPECT_EQ(std::get<0>(room.shouldStartBoilerAndHeat()), heating::Room::TemperatureStatus::TEMPERATURE_OK);
	EXPECT_NE(room.getStatus().find("OVERRIDE"), std::string::npos);
}

TEST(AdaptiveRoom, ShadowAndDisabledNeverChangeSetpoint) {
	for (bool enabled : {false, true}) {
		auto config = roomConfig(true);
		config.adaptiveStart_.optimalStartEnabled = enabled;
		heating::Room room({}, config);
		room.storeTemperature(1900);
		EXPECT_EQ(std::get<0>(room.shouldStartBoilerAndHeat()), heating::Room::TemperatureStatus::TEMPERATURE_OK);
	}
}

TEST(AdaptiveRoom, MissingTemperatureAndReloadPreserveOverride) {
	heating::Room previous({}, roomConfig());
	EXPECT_EQ(std::get<0>(previous.shouldStartBoilerAndHeat()), heating::Room::TemperatureStatus::MISSING_TEMPERATURE);
	previous.storeTemperature(1900);
	previous.createTemporaryOverride(1700, 600);
	heating::Room updated({}, roomConfig());
	updated.inheritThermalState(previous);
	EXPECT_EQ(std::get<0>(updated.shouldStartBoilerAndHeat()), heating::Room::TemperatureStatus::TEMPERATURE_OK);
	EXPECT_NE(updated.getStatus().find("OVERRIDE"), std::string::npos);
}

namespace {
class ScopedTimezone {
	std::optional<std::string> previous_;

public:
	explicit ScopedTimezone(const char *zone) {
		if (const auto *old = std::getenv("TZ"))
			previous_ = old;
		setenv("TZ", zone, 1);
		tzset();
	}
	~ScopedTimezone() {
		if (previous_)
			setenv("TZ", previous_->c_str(), 1);
		else
			unsetenv("TZ");
		tzset();
	}
};
std::time_t localInstant(int year, int month, int day, int hour, int minute, int dst = -1) {
	struct tm local{};
	local.tm_year = year - 1900;
	local.tm_mon = month - 1;
	local.tm_mday = day;
	local.tm_hour = hour;
	local.tm_min = minute;
	local.tm_isdst = dst;
	return std::mktime(&local);
}
heating::RoomConfig::TemperatureSetting setting(uint16_t from, uint16_t to, int16_t temperature) {
	heating::RoomConfig::TemperatureSetting s;
	s.timeFrom_ = from;
	s.timeTo_ = to;
	s.temperature_ = temperature;
	return s;
}
int16_t effectiveAt(const heating::RoomConfig &config, int64_t instant) {
	const std::time_t t = instant;
	struct tm local{};
	localtime_r(&t, &local);
	int16_t value = -32768;
	for (const auto &s : config.temperatures_)
		if (s.enabled_ && s.doesFit(local.tm_hour * 100 + local.tm_min, local.tm_wday))
			value = std::max(value, s.temperature_);
	return value == -32768 ? config.baseTemperature_ : value;
}
} // namespace

TEST(AdaptiveSchedule, WeekRolloverAndInclusiveEndOfOvernightRange) {
	ScopedTimezone timezone("Europe/Warsaw");
	auto config = roomConfig();
	config.temperatures_.clear();
	config.baseTemperature_ = 2100;
	auto night = setting(2200, 600, 1800);
	config.temperatures_.push_back(night);
	const auto now = localInstant(2026, 10, 18, 23, 0); // Sunday
	const auto event = nextIncrease(now, [&](int64_t t) { return effectiveAt(config, t); });
	EXPECT_EQ(event.targetTime, localInstant(2026, 10, 19, 6, 1));
	EXPECT_EQ(event.previousTime, localInstant(2026, 10, 18, 22, 0));
	EXPECT_EQ(event.temperature, 2100);
	config.temperatures_.clear();
	config.baseTemperature_ = 1800;
	auto monday = setting(600, 800, 2100);
	monday.days_.fill(false);
	monday.days_[1] = true;
	config.temperatures_.push_back(monday);
	EXPECT_EQ(nextIncrease(now, [&](int64_t t) { return effectiveAt(config, t); }).targetTime, localInstant(2026, 10, 19, 6, 0));
}

TEST(AdaptiveSchedule, OverlapResolvesNextEffectiveIncrease) {
	ScopedTimezone timezone("UTC0");
	auto config = roomConfig();
	config.temperatures_.clear();
	config.temperatures_.push_back(setting(500, 700, 1900));
	config.temperatures_.push_back(setting(600, 800, 2100));
	const auto now = localInstant(2026, 10, 20, 4, 30);
	heating::Room room({}, config, [now] { return now; });
	room.storeTemperature(1850);
	EXPECT_EQ(std::get<0>(room.shouldStartBoilerAndHeat()), heating::Room::TemperatureStatus::START_HEATING);
	EXPECT_NE(room.getStatus().find("\"tempSet\": 1900"), std::string::npos);
}

TEST(AdaptiveSchedule, DstSpringGapAndAutumnRepeatedHour) {
	ScopedTimezone timezone("Europe/Warsaw");
	auto config = roomConfig();
	config.temperatures_.clear();
	config.temperatures_.push_back(setting(230, 400, 2100));
	const auto spring = localInstant(2026, 3, 29, 1, 30);
	EXPECT_EQ(nextIncrease(spring, [&](int64_t t) { return effectiveAt(config, t); }).targetTime, localInstant(2026, 3, 29, 3, 0));
	const auto autumn = localInstant(2026, 10, 25, 2, 45, 1);
	const auto event = nextIncrease(autumn, [&](int64_t t) { return effectiveAt(config, t); });
	EXPECT_EQ(event.targetTime, localInstant(2026, 10, 25, 2, 30, 0));
	EXPECT_EQ(event.previousTime, localInstant(2026, 10, 25, 2, 0, 0));
}

TEST(AdaptiveSchedule, ClockJumpRecalculatesAndInvalidClockDisablesPreheat) {
	ScopedTimezone timezone("UTC0");
	auto config = roomConfig();
	config.temperatures_ = {setting(600, 800, 2100)};
	auto now = localInstant(2026, 10, 20, 4, 0);
	heating::Room room({}, config, [&now] { return now; });
	room.storeTemperature(1900);
	EXPECT_EQ(std::get<0>(room.shouldStartBoilerAndHeat()), heating::Room::TemperatureStatus::START_HEATING);
	now = localInstant(2026, 10, 20, 1, 0);
	EXPECT_EQ(std::get<0>(room.shouldStartBoilerAndHeat()), heating::Room::TemperatureStatus::TEMPERATURE_OK);
	EXPECT_NE(room.getStatus().find("WAITING"), std::string::npos);
	now = 0;
	EXPECT_EQ(std::get<0>(room.shouldStartBoilerAndHeat()), heating::Room::TemperatureStatus::TEMPERATURE_OK);
	EXPECT_NE(room.getStatus().find("NO_DATA"), std::string::npos);
}

TEST(AdaptiveRoom, LearningIsIndependentAndResetClearsWorkingHistory) {
	auto config = roomConfig();
	config.adaptiveStart_.thermalLearningEnabled = true;
	config.adaptiveStart_.optimalStartEnabled = false;
	heating::Room room({}, config);
	room.storeTemperature(1900);
	room.updateThermalContext(0, 50, 55, true, true);
	room.shouldStartBoilerAndHeat();
	EXPECT_NE(room.getStatus().find("\"historySamples\": 1"), std::string::npos);
	EXPECT_NE(room.getStatus().find("DISABLED"), std::string::npos);
	EXPECT_TRUE(room.resetThermalModel());
	EXPECT_NE(room.getStatus().find("\"historySamples\": 0"), std::string::npos);
}

TEST(OptimalStart, BenchmarkAgainstScheduleAndFixedTwoHourAdvance) {
	// Known 1R1C plants, independent one-minute replay with explicit response delay.
	// Compare arrival error rather than merely arriving before the deadline.
	float adaptiveAbsolute = 0, fixedAbsolute = 0, scheduledAbsolute = 0;
	for (float gain : {2.5f, 5.0f, 8.0f}) {
		ThermalModel model;
		model.state.gain = gain;
		model.state.loss = 0.025f;
		model.state.heatingWindows = 12;
		model.state.coolingWindows = 6;
		const auto prediction = model.predict(18, 21, 0, 55);
		ASSERT_FALSE(prediction.unreachable);
		float room = 18;
		int duration = 10;
		while (room < 21 && duration < 1440) {
			room += (gain * std::pow(std::max(0.0f, 55 - room - 5) / 50, 1.3f) - 0.025f * room) / 60;
			++duration;
		}
		adaptiveAbsolute += std::abs(duration - prediction.minutes);
		fixedAbsolute += std::abs(duration - 120);
		scheduledAbsolute += duration;
	}
	EXPECT_LT(adaptiveAbsolute, fixedAbsolute);
	EXPECT_LT(adaptiveAbsolute, scheduledAbsolute);
	EXPECT_LE(adaptiveAbsolute, 3);
}
