#include <gtest/gtest.h>
#include <limits>
#include "RoomConfig.h"

// ============================================================================
// TemperatureSetting::doesFit Tests
// ============================================================================

class DoesFitTest : public ::testing::Test {
protected:
	heating::RoomConfig::TemperatureSetting makeSetting(uint16_t from, uint16_t to, std::array<bool, 7> days = {{true, true, true, true, true, true, true}}) {
		heating::RoomConfig::TemperatureSetting ts;
		ts.timeFrom_ = from;
		ts.timeTo_ = to;
		ts.days_ = days;
		ts.enabled_ = true;
		ts.temperature_ = 2200;
		ts.name_ = "test";
		return ts;
	}
};

TEST_F(DoesFitTest, NormalRange_Inside) {
	auto ts = makeSetting(800, 1700); // 08:00 - 17:00
	EXPECT_TRUE(ts.doesFit(800, 1));   // exactly at start
	EXPECT_TRUE(ts.doesFit(1200, 1));  // middle
	EXPECT_TRUE(ts.doesFit(1700, 1));  // exactly at end
}

TEST_F(DoesFitTest, NormalRange_Outside) {
	auto ts = makeSetting(800, 1700);
	EXPECT_FALSE(ts.doesFit(759, 1));  // before start
	EXPECT_FALSE(ts.doesFit(1701, 1)); // after end
	EXPECT_FALSE(ts.doesFit(0, 1));
	EXPECT_FALSE(ts.doesFit(2359, 1));
}

TEST_F(DoesFitTest, OvernightRange_Inside) {
	auto ts = makeSetting(2230, 600); // 22:30 - 06:00
	EXPECT_TRUE(ts.doesFit(2230, 1)); // exactly at start
	EXPECT_TRUE(ts.doesFit(2359, 1)); // before midnight
	EXPECT_TRUE(ts.doesFit(0, 1));    // midnight
	EXPECT_TRUE(ts.doesFit(300, 1));  // middle of night
	EXPECT_TRUE(ts.doesFit(600, 1));  // exactly at end
}

TEST_F(DoesFitTest, OvernightRange_Outside) {
	auto ts = makeSetting(2230, 600);
	EXPECT_FALSE(ts.doesFit(700, 1));   // morning after
	EXPECT_FALSE(ts.doesFit(1200, 1));  // midday
	EXPECT_FALSE(ts.doesFit(2229, 1));  // just before start
}

TEST_F(DoesFitTest, DayOfWeekFilter) {
	std::array<bool, 7> days = {{false, true, false, true, false, false, false}};
	auto ts = makeSetting(800, 1700, days);

	EXPECT_TRUE(ts.doesFit(1200, 1));   // Monday
	EXPECT_TRUE(ts.doesFit(1200, 3));   // Wednesday
	EXPECT_FALSE(ts.doesFit(1200, 0));  // Sunday
	EXPECT_FALSE(ts.doesFit(1200, 2));  // Tuesday
	EXPECT_FALSE(ts.doesFit(1200, 4));  // Thursday
}

TEST_F(DoesFitTest, InvalidTime) {
	auto ts = makeSetting(800, 1700);
	EXPECT_FALSE(ts.doesFit(2400, 1));
	EXPECT_FALSE(ts.doesFit(2500, 1));
}

TEST_F(DoesFitTest, FullDay) {
	auto ts = makeSetting(0, 2359); // 00:00 - 23:59
	EXPECT_TRUE(ts.doesFit(0, 1));
	EXPECT_TRUE(ts.doesFit(1200, 1));
	EXPECT_TRUE(ts.doesFit(2359, 1));
}

// ============================================================================
// Temperature Set Selection Tests
// ============================================================================

// Reimplementation of getTemperatureSet logic for native testing
static std::pair<int16_t, const heating::RoomConfig::TemperatureSetting *>
getTemperatureSet(heating::RoomConfig const &config, uint16_t currentTime, uint8_t dayOfTheWeek) {
	if (config.temperatures_.empty()) {
		return {config.baseTemperature_, nullptr};
	}

	int16_t value = std::numeric_limits<int16_t>::min();
	const heating::RoomConfig::TemperatureSetting *program = nullptr;

	for (auto const &temp : config.temperatures_) {
		if (temp.isEnabled() && temp.doesFit(currentTime, dayOfTheWeek)) {
			if (temp.getTemperature() > value) {
				value = temp.getTemperature();
				program = &temp;
			}
		}
	}

	if (value == std::numeric_limits<int16_t>::min()) {
		value = config.baseTemperature_;
	}

	return {value, program};
}

class TemperatureSetTest : public ::testing::Test {
protected:
	heating::RoomConfig makeConfig(int16_t baseTemp = 2000) {
		heating::RoomConfig cfg;
		cfg.baseTemperature_ = baseTemp;
		cfg.enabled_ = true;
		cfg.name_ = "TestRoom";
		cfg.sensorAddress_ = {};
		return cfg;
	}

	heating::RoomConfig::TemperatureSetting makeSetting(std::string name, uint16_t from, uint16_t to, int16_t temp, bool enabled = true) {
		heating::RoomConfig::TemperatureSetting ts;
		ts.name_ = std::move(name);
		ts.timeFrom_ = from;
		ts.timeTo_ = to;
		ts.temperature_ = temp;
		ts.enabled_ = enabled;
		ts.days_ = {{true, true, true, true, true, true, true}};
		return ts;
	}
};

TEST_F(TemperatureSetTest, NoPrograms_ReturnsBase) {
	auto cfg = makeConfig(2000);
	auto [temp, prog] = getTemperatureSet(cfg, 1200, 1);
	EXPECT_EQ(temp, 2000);
	EXPECT_EQ(prog, nullptr);
}

TEST_F(TemperatureSetTest, SingleProgram_Inside) {
	auto cfg = makeConfig(2000);
	cfg.temperatures_.push_back(makeSetting("Day", 800, 1700, 2200));

	auto [temp, prog] = getTemperatureSet(cfg, 1200, 1);
	EXPECT_EQ(temp, 2200);
	ASSERT_NE(prog, nullptr);
	EXPECT_EQ(prog->name_, "Day");
}

TEST_F(TemperatureSetTest, SingleProgram_Outside) {
	auto cfg = makeConfig(2000);
	cfg.temperatures_.push_back(makeSetting("Day", 800, 1700, 2200));

	auto [temp, prog] = getTemperatureSet(cfg, 1800, 1);
	EXPECT_EQ(temp, 2000);
	EXPECT_EQ(prog, nullptr);
}

TEST_F(TemperatureSetTest, OverlappingPrograms_PicksHighest) {
	auto cfg = makeConfig(1800);
	cfg.temperatures_.push_back(makeSetting("Low", 800, 2000, 2100));
	cfg.temperatures_.push_back(makeSetting("High", 1000, 1400, 2400));

	auto [temp, prog] = getTemperatureSet(cfg, 1200, 1);
	EXPECT_EQ(temp, 2400);
	ASSERT_NE(prog, nullptr);
	EXPECT_EQ(prog->name_, "High");
}

TEST_F(TemperatureSetTest, DisabledProgram_Ignored) {
	auto cfg = makeConfig(1800);
	cfg.temperatures_.push_back(makeSetting("Disabled", 800, 1700, 2500, false));

	auto [temp, prog] = getTemperatureSet(cfg, 1200, 1);
	EXPECT_EQ(temp, 1800);
	EXPECT_EQ(prog, nullptr);
}

TEST_F(TemperatureSetTest, OvernightProgram) {
	auto cfg = makeConfig(1800);
	cfg.temperatures_.push_back(makeSetting("Night", 2200, 600, 1900));

	auto [temp, prog] = getTemperatureSet(cfg, 2300, 1);
	EXPECT_EQ(temp, 1900);
	ASSERT_NE(prog, nullptr);

	auto [temp2, prog2] = getTemperatureSet(cfg, 300, 1);
	EXPECT_EQ(temp2, 1900);

	auto [temp3, prog3] = getTemperatureSet(cfg, 1200, 1);
	EXPECT_EQ(temp3, 1800);
}
