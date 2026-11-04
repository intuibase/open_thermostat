#include <gtest/gtest.h>
#include "AosDiagnostics.h"
#include "Room.h"
#include <cJSON.h>
#include <cstdarg>
#include <cstdio>
#include <sstream>

namespace {
class CaptureAosLogger : public ib::logger::LoggerInterface {
public:
	mutable std::vector<std::string> records;
	LogFeature enabled;
	std::unordered_map<LogFeatureType, std::string> features;
	void printf(LogLevel, const char *, ...) const override {}
	void printf(LogLevel level, LogFeatureType feature, const char *format, ...) const override {
		if (!doesFeatureMeetsLevelCondition(level, feature) || getFeatureName(feature) != "AOS")
			return;
		va_list args;
		va_start(args, format);
		char buffer[8192];
		const auto length = vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		if (length >= 0 && static_cast<size_t>(length) < sizeof(buffer))
			records.emplace_back(buffer, length);
	}
	bool doesMeetsLevelCondition(LogLevel) const override { return true; }
	bool doesFeatureMeetsLevelCondition(LogLevel, LogFeatureType feature) const override { return enabled[feature]; }
	LogLevel getMaxLogLevel() const override { return LogLevel::TRACE; }
	void setLogFeatures(LogFeature value) override { enabled = value; }
	LogFeatureType addFeature(std::string name, bool active = true) override {
		for (const auto &[id, value] : features)
			if (value == name)
				return id;
		const auto id = static_cast<LogFeatureType>(features.size());
		features[id] = name;
		enabled[id] = active;
		return id;
	}
	std::string getFeatureName(LogFeatureType id) const override {
		auto found = features.find(id);
		return found == features.end() ? "" : found->second;
	}
	std::unordered_map<LogFeatureType, std::string> getRegisteredFeatures() const override { return features; }
	bool isFeatureEnabled(LogFeatureType id) const override { return enabled[id]; }
	void enableFeature(LogFeatureType id, bool value) override { enabled[id] = value; }
	void attachSink(std::shared_ptr<ib::logger::LoggerSinkInterface>) override {}
};
} // namespace

TEST(AosDiagnostics, JsonEscapingNullsAndCadence) {
	std::ostringstream output;
	heating::aos::DiagnosticJson json(output);
	json.text("room", "Łazienka \"A\"\\\n\t\x01");
	json.number("missing", NAN);
	json.boolean("unknown", std::optional<bool>{});
	json.number("ts", 1792520000LL);
	json.finish();
	auto parsed = cJSON_Parse(output.str().c_str());
	ASSERT_NE(parsed, nullptr);
	EXPECT_STREQ(cJSON_GetObjectItem(parsed, "room")->valuestring, "Łazienka \"A\"\\\n\t\x01");
	EXPECT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(parsed, "missing")));
	EXPECT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(parsed, "unknown")));
	cJSON_Delete(parsed);
	heating::aos::DiagnosticCadence cadence;
	EXPECT_TRUE(cadence.due(0));
	cadence.mark(0);
	EXPECT_FALSE(cadence.due(59));
	EXPECT_TRUE(cadence.due(60));
}

TEST(AosDiagnostics, ProductionRoomFeatureIsIndependentAndCanBeDisabled) {
	auto logger = std::make_shared<CaptureAosLogger>();
	logger->addFeature("Room", false);
	heating::RoomConfig config{};
	config.id_ = "aabbccdd";
	config.name_ = "Łazienka \"A\"\n";
	config.enabled_ = true;
	config.adaptiveStart_.thermalLearningEnabled = true;
	const auto wall = static_cast<std::time_t>(1792520000);
	heating::Room room(logger, config, [wall] { return wall; });
	ASSERT_EQ(logger->records.size(), 1u);
	EXPECT_NE(logger->records[0].find("\"event\":\"config\""), std::string::npos);
	room.storeTemperature(2000);
	heating::aos::HeatContext context;
	context.usesEms = true;
	context.boilerCommanded = true;
	context.invalidReason = "ems_activity_missing";
	room.updateThermalContext(0, {}, 50, false, false, context);
	room.shouldStartBoilerAndHeat();
	room.logAdaptiveSnapshot();
	const auto count = logger->records.size();
	ASSERT_GE(count, 3u);
	room.logAdaptiveSnapshot();
	EXPECT_EQ(logger->records.size(), count);
	for (const auto &line : logger->records) {
		ASSERT_EQ(line.find("AOS_DIAG "), 0u);
		EXPECT_LE(line.size() + 64, heating::aos::diagnosticMaxRecordBytes);
		auto json = cJSON_Parse(line.c_str() + 9);
		ASSERT_NE(json, nullptr) << line;
		EXPECT_STREQ(cJSON_GetObjectItem(json, "room_id")->valuestring, "aabbccdd");
		EXPECT_STREQ(cJSON_GetObjectItem(json, "room")->valuestring, config.name_.c_str());
		EXPECT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(json, "actual_supply_c")));
		if (std::string(cJSON_GetObjectItem(json, "event")->valuestring) == "sample") {
			EXPECT_STREQ(cJSON_GetObjectItem(json, "sample_reason")->valuestring, "ems_activity_missing");
			EXPECT_STREQ(cJSON_GetObjectItem(json, "observation")->valuestring, "invalid_sample");
		}
		cJSON_Delete(json);
	}
	logger->enableFeature(logger->addFeature("AOS"), false);
	room.logAdaptiveSnapshot();
	EXPECT_TRUE(room.resetThermalModel());
	EXPECT_EQ(logger->records.size(), count);
}

TEST(AosDiagnostics, ModelReportsAcceptedFallbackAndRejectedSamples) {
	heating::aos::ThermalModel model;
	heating::aos::ThermalHistory history;
	model.observe(history);
	EXPECT_STREQ(model.observation.reason, "empty_history");
	bool accepted = false;
	for (int i = 0; i < 30; ++i) {
		history.push({i * 300, 15 + i * 0.15f, NAN, NAN, true, true});
		model.observe(history);
		if (model.observation.accepted) {
			accepted = true;
			EXPECT_STREQ(model.observation.reason, "fallback_missing_inputs");
			EXPECT_TRUE(model.observation.windowChecked);
			EXPECT_TRUE(std::isnan(model.observation.emission));
			EXPECT_NEAR(model.observation.rate, 1.8f, 0.01f);
		}
	}
	EXPECT_TRUE(accepted);
	history.push({9000, 20, NAN, NAN, true, false});
	model.observe(history);
	EXPECT_FALSE(model.observation.accepted);
	EXPECT_STREQ(model.observation.reason, "invalid_sample");
}
