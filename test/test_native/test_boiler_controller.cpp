#include <gtest/gtest.h>
#include "BoilerController.h"

#include <vector>

// ============================================================================
// GPIO mock / recorder
// ============================================================================

namespace {

struct GpioRecorder {
	struct PinWrite { uint8_t pin; uint8_t value; };
	std::vector<PinWrite> writes;
	std::vector<std::pair<uint8_t, uint8_t>> modes;

	heating::BoilerController::GpioOps makeOps() {
		return heating::BoilerController::GpioOps{
			[this](uint8_t pin, uint8_t mode) { modes.push_back({pin, mode}); },
			[this](uint8_t pin, uint8_t value) { writes.push_back({pin, value}); },
			[](uint32_t) {}, // no-op delay
			0x03, // PIN_OUTPUT
			0x00, // PIN_LOW
			0x01  // PIN_HIGH
		};
	}

	void clear() { writes.clear(); modes.clear(); }

	bool lastWriteFor(uint8_t pin, uint8_t &value) const {
		for (auto it = writes.rbegin(); it != writes.rend(); ++it) {
			if (it->pin == pin) { value = it->value; return true; }
		}
		return false;
	}
};



// ============================================================================
// Test fixtures
// ============================================================================

class BoilerOnOffTest : public ::testing::Test {
protected:
	config::BoilerConfig cfg;
	config::valves_t valves = {{10, 11, 12, 13, 14, 15, 16, 17}};
	uint8_t boilerPin = 5;
	GpioRecorder gpio;
	int16_t outdoorTemp = 500; // 5°C

	void SetUp() override {
		cfg.boiler.controlMode = config::BoilerConfig::controlMode_t::onoff;
		cfg.boiler.valvePreheatingDelay = 0;
		cfg.heatingCurve.heatingCurve = {{20, 25, 30, 35, 45, 55, 65, 75, 80}};
	}

	heating::BoilerController makeController() {
		return heating::BoilerController(cfg, valves, boilerPin,
			[this]() { return outdoorTemp; },
			[](bool, uint8_t) {},
			[](uint8_t) {},
			gpio.makeOps());
	}
};

// ============================================================================
// ON/OFF mode tests
// ============================================================================

TEST_F(BoilerOnOffTest, StartsOff) {
	auto bc = makeController();
	std::stringstream ss;
	bc.getStatus(ss);
	EXPECT_NE(ss.str().find("\"boiler\": false"), std::string::npos);
}

TEST_F(BoilerOnOffTest, StartBoiler) {
	auto bc = makeController();
	gpio.clear();

	bc.startBoilerOrContinue(true, false, std::nullopt);

	// Boiler pin should be set LOW (active low)
	uint8_t val = 0xFF;
	ASSERT_TRUE(gpio.lastWriteFor(boilerPin, val));
	EXPECT_EQ(val, 0x00); // PIN_LOW

	std::stringstream ss;
	bc.getStatus(ss);
	EXPECT_NE(ss.str().find("\"boiler\": true"), std::string::npos);
}

TEST_F(BoilerOnOffTest, StopBoiler) {
	auto bc = makeController();
	bc.startBoilerOrContinue(true, false, std::nullopt);

	gpio.clear();
	bc.startBoilerOrContinue(false, false, std::nullopt);

	uint8_t val = 0xFF;
	ASSERT_TRUE(gpio.lastWriteFor(boilerPin, val));
	EXPECT_EQ(val, 0x01); // PIN_HIGH

	std::stringstream ss;
	bc.getStatus(ss);
	EXPECT_NE(ss.str().find("\"boiler\": false"), std::string::npos);
}

TEST_F(BoilerOnOffTest, ContinueHeating_KeepsOn) {
	auto bc = makeController();
	bc.startBoilerOrContinue(true, false, std::nullopt);

	// shouldStart=false, shouldContinue=true → stays on
	bc.startBoilerOrContinue(false, true, std::nullopt);

	std::stringstream ss;
	bc.getStatus(ss);
	EXPECT_NE(ss.str().find("\"boiler\": true"), std::string::npos);
}

TEST_F(BoilerOnOffTest, NeitherStartNorContinue_TurnsOff) {
	auto bc = makeController();
	bc.startBoilerOrContinue(true, false, std::nullopt);

	bc.startBoilerOrContinue(false, false, std::nullopt);

	std::stringstream ss;
	bc.getStatus(ss);
	EXPECT_NE(ss.str().find("\"boiler\": false"), std::string::npos);
}

// ============================================================================
// Valve preheating tests
// ============================================================================

class ValvePreheatingTest : public BoilerOnOffTest {
protected:
	void SetUp() override {
		BoilerOnOffTest::SetUp();
		cfg.boiler.valvePreheatingDelay = 30; // 30 seconds
	}
};

TEST_F(ValvePreheatingTest, StartsPreheating_BeforeBoiler) {
	auto bc = makeController();

	bc.startBoilerOrContinue(true, false, std::nullopt);

	// Boiler should NOT be started yet — preheating
	std::stringstream ss;
	bc.getStatus(ss);
	EXPECT_NE(ss.str().find("\"boiler\": false"), std::string::npos);
}

TEST_F(ValvePreheatingTest, PreheatingInProgress_DoesNotStartBoiler) {
	auto bc = makeController();

	bc.startBoilerOrContinue(true, false, std::nullopt);

	// Call again — should still be preheating
	bc.startBoilerOrContinue(true, false, std::nullopt);

	std::stringstream ss;
	bc.getStatus(ss);
	EXPECT_NE(ss.str().find("\"boiler\": false"), std::string::npos);
}

TEST_F(ValvePreheatingTest, NoPreheating_WhenDelayZero) {
	cfg.boiler.valvePreheatingDelay = 0;
	auto bc = makeController();

	bc.startBoilerOrContinue(true, false, std::nullopt);

	std::stringstream ss;
	bc.getStatus(ss);
	EXPECT_NE(ss.str().find("\"boiler\": true"), std::string::npos);
}

// ============================================================================
// Valve handling tests
// ============================================================================

TEST_F(BoilerOnOffTest, HandleValves_AllOpen_WhenBoilerOff) {
	auto bc = makeController();
	gpio.clear();

	bc.handleValves({0, 2, 4}); // these should be ignored — boiler is off

	// All valves should be opened (HIGH)
	for (auto const &w : gpio.writes) {
		EXPECT_EQ(w.value, 0x01);
	}
}

TEST_F(BoilerOnOffTest, HandleValves_ClosesSpecified_WhenBoilerOn) {
	auto bc = makeController();
	bc.startBoilerOrContinue(true, false, std::nullopt);
	gpio.clear();

	bc.handleValves({1, 3}); // close valves 1 and 3

	// Check GPIO writes for valve pins
	uint8_t v0, v1, v2, v3;
	ASSERT_TRUE(gpio.lastWriteFor(valves[0], v0));
	ASSERT_TRUE(gpio.lastWriteFor(valves[1], v1));
	ASSERT_TRUE(gpio.lastWriteFor(valves[2], v2));
	ASSERT_TRUE(gpio.lastWriteFor(valves[3], v3));

	EXPECT_EQ(v0, 0x01); // open
	EXPECT_EQ(v1, 0x00); // closed
	EXPECT_EQ(v2, 0x01); // open
	EXPECT_EQ(v3, 0x00); // closed
}

// ============================================================================
// EMS mode tests
// ============================================================================

class BoilerEmsTest : public ::testing::Test {
protected:
	config::BoilerConfig cfg;
	config::valves_t valves = {{10, 11, 12, 13, 14, 15, 16, 17}};
	uint8_t boilerPin = 5;
	GpioRecorder gpio;
	int16_t outdoorTemp = 500; // 5°C

	bool lastEmsEnabled = false;
	uint8_t lastEmsFlowTemp = 0;
	uint8_t lastEmsHeatingTemp = 0;

	void SetUp() override {
		cfg.boiler.controlMode = config::BoilerConfig::controlMode_t::ems;
		cfg.boiler.valvePreheatingDelay = 0;
		cfg.heatingCurve.heatingCurve = {{20, 25, 30, 35, 45, 55, 65, 75, 80}};
		cfg.heatingCurve.maxHeatingCurveTemp = 90;
	}

	heating::BoilerController makeController() {
		return heating::BoilerController(cfg, valves, boilerPin,
			[this]() { return outdoorTemp; },
			[this](bool enabled, uint8_t flowTemp) { lastEmsEnabled = enabled; lastEmsFlowTemp = flowTemp; },
			[this](uint8_t temp) { lastEmsHeatingTemp = temp; },
			gpio.makeOps());
	}
};

TEST_F(BoilerEmsTest, StartBoiler_SendsEmsCommand) {
	auto bc = makeController();

	bc.startBoilerOrContinue(true, false, std::nullopt);

	EXPECT_TRUE(lastEmsEnabled);
	EXPECT_EQ(lastEmsHeatingTemp, 90); // maxHeatingCurveTemp
	// At 5°C outdoor, curve gives 35°C
	EXPECT_EQ(lastEmsFlowTemp, 35);
}

TEST_F(BoilerEmsTest, HeatingTempOverride_TakesHigher) {
	outdoorTemp = 500; // 5°C → curve gives 35°C
	auto bc = makeController();

	bc.startBoilerOrContinue(true, false, 50); // override 50°C

	// Override 50°C (5000) > curve 35°C (3500) → uses override
	EXPECT_EQ(lastEmsFlowTemp, 50);
}

TEST_F(BoilerEmsTest, HeatingTempOverride_CurveTakesWhenHigher) {
	outdoorTemp = -1500; // -15°C → curve gives 75°C
	auto bc = makeController();

	bc.startBoilerOrContinue(true, false, 50); // override 50°C

	// Curve 75°C > override 50°C → uses curve
	EXPECT_EQ(lastEmsFlowTemp, 75);
}

TEST_F(BoilerEmsTest, StopBoiler_SendsEmsDisable) {
	auto bc = makeController();
	bc.startBoilerOrContinue(true, false, std::nullopt);
	ASSERT_TRUE(lastEmsEnabled);

	bc.startBoilerOrContinue(false, false, std::nullopt);
	EXPECT_FALSE(lastEmsEnabled);
}

// ============================================================================
// ON/OFF outdoor mode tests
// ============================================================================

class BoilerOnOffOutdoorTest : public ::testing::Test {
protected:
	config::BoilerConfig cfg;
	config::valves_t valves = {{10, 11, 12, 13, 14, 15, 16, 17}};
	uint8_t boilerPin = 5;
	GpioRecorder gpio;
	int16_t outdoorTemp = 500;
	uint8_t lastSetHeatingTemp = 0;

	void SetUp() override {
		cfg.boiler.controlMode = config::BoilerConfig::controlMode_t::onoff_outdoor;
		cfg.boiler.valvePreheatingDelay = 0;
		cfg.heatingCurve.heatingCurve = {{20, 25, 30, 35, 45, 55, 65, 75, 80}};
	}

	heating::BoilerController makeController() {
		return heating::BoilerController(cfg, valves, boilerPin,
			[this]() { return outdoorTemp; },
			[](bool, uint8_t) {},
			[this](uint8_t temp) { lastSetHeatingTemp = temp; },
			gpio.makeOps());
	}
};

TEST_F(BoilerOnOffOutdoorTest, SetsHeatingTemperatureFromCurve) {
	outdoorTemp = 500; // 5°C → curve gives 35°C
	auto bc = makeController();

	bc.startBoilerOrContinue(true, false, std::nullopt);

	EXPECT_EQ(lastSetHeatingTemp, 35);

	// Also sets boiler pin LOW
	uint8_t val = 0xFF;
	ASSERT_TRUE(gpio.lastWriteFor(boilerPin, val));
	EXPECT_EQ(val, 0x00);
}

TEST_F(BoilerOnOffOutdoorTest, UpdatesOnContinue) {
	outdoorTemp = 500;
	auto bc = makeController();
	bc.startBoilerOrContinue(true, false, std::nullopt);
	EXPECT_EQ(lastSetHeatingTemp, 35);

	// Temperature drops
	outdoorTemp = -500; // -5°C → 55°C
	bc.startBoilerOrContinue(false, true, std::nullopt);
	// onoff_outdoor updates heating temp even on continue
	EXPECT_EQ(lastSetHeatingTemp, 55);
}

} // anonymous namespace
