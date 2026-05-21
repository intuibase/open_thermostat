#include <gtest/gtest.h>
#include "BoilerController.h"
#include "GpioPort.h"

#include <vector>
#include <memory>

// ============================================================================
// GPIO mock
// ============================================================================

namespace {

struct MockGpioPort : public gpio::GpioPort {
	int id;
	bool initialized = false;
	bool lastValue = false;
	int writeCount = 0;

	explicit MockGpioPort(int id) : id(id) {}

	void initOutput() override { initialized = true; }
	void write(bool high) override {
		lastValue = high;
		writeCount++;
	}
};

// Helper: create valve ports and keep raw pointers for assertions
struct ValveMocks {
	std::vector<MockGpioPort *> raw;

	std::vector<std::unique_ptr<gpio::GpioPort>> makePorts(int count) {
		std::vector<std::unique_ptr<gpio::GpioPort>> ports;
		for (int i = 0; i < count; i++) {
			auto p = std::make_unique<MockGpioPort>(i);
			raw.push_back(p.get());
			ports.push_back(std::move(p));
		}
		return ports;
	}
};

// ============================================================================
// Test fixtures
// ============================================================================

class BoilerOnOffTest : public ::testing::Test {
protected:
	config::BoilerConfig cfg;
	MockGpioPort *boilerGpio = nullptr;
	ValveMocks valveMocks;
	int16_t outdoorTemp = 500; // 5°C

	void SetUp() override {
		cfg.boiler.controlMode = config::BoilerConfig::controlMode_t::onoff;
		cfg.boiler.valvePreheatingDelay = 0;
		cfg.heatingCurve.heatingCurve = {{20, 25, 30, 35, 45, 55, 65, 75, 80}};
	}

	heating::BoilerController makeController() {
		auto bp = std::make_unique<MockGpioPort>(99);
		boilerGpio = bp.get();
		return heating::BoilerController(cfg, [this]() { return outdoorTemp; }, [](bool, uint8_t) {}, [](uint8_t) {}, std::move(bp), valveMocks.makePorts(8), {});
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

	bc.startBoilerOrContinue(true, false, std::nullopt);

	// Boiler port should be written (enabled)
	EXPECT_TRUE(boilerGpio->lastValue);

	std::stringstream ss;
	bc.getStatus(ss);
	EXPECT_NE(ss.str().find("\"boiler\": true"), std::string::npos);
}

TEST_F(BoilerOnOffTest, StopBoiler) {
	auto bc = makeController();
	bc.startBoilerOrContinue(true, false, std::nullopt);

	bc.startBoilerOrContinue(false, false, std::nullopt);

	EXPECT_FALSE(boilerGpio->lastValue);

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

	bc.handleValves({0, 2, 4}); // these should be ignored — boiler is off

	// All valves should be opened (write false = open)
	for (auto *v : valveMocks.raw) {
		EXPECT_FALSE(v->lastValue);
	}
}

TEST_F(BoilerOnOffTest, HandleValves_ClosesSpecified_WhenBoilerOn) {
	auto bc = makeController();
	bc.startBoilerOrContinue(true, false, std::nullopt);

	bc.handleValves({1, 3}); // close valves 1 and 3

	EXPECT_FALSE(valveMocks.raw[0]->lastValue); // open
	EXPECT_TRUE(valveMocks.raw[1]->lastValue);  // closed
	EXPECT_FALSE(valveMocks.raw[2]->lastValue); // open
	EXPECT_TRUE(valveMocks.raw[3]->lastValue);  // closed
}

// ============================================================================
// EMS mode tests
// ============================================================================

class BoilerEmsTest : public ::testing::Test {
protected:
	config::BoilerConfig cfg;
	MockGpioPort *boilerGpio = nullptr;
	ValveMocks valveMocks;
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
		auto bp = std::make_unique<MockGpioPort>(99);
		boilerGpio = bp.get();
		return heating::BoilerController(
			cfg, [this]() { return outdoorTemp; },
			[this](bool enabled, uint8_t flowTemp) {
				lastEmsEnabled = enabled;
				lastEmsFlowTemp = flowTemp;
			},
			[this](uint8_t temp) { lastEmsHeatingTemp = temp; }, std::move(bp), valveMocks.makePorts(8), {});
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
	MockGpioPort *boilerGpio = nullptr;
	ValveMocks valveMocks;
	int16_t outdoorTemp = 500;
	uint8_t lastSetHeatingTemp = 0;

	void SetUp() override {
		cfg.boiler.controlMode = config::BoilerConfig::controlMode_t::onoff_outdoor;
		cfg.boiler.valvePreheatingDelay = 0;
		cfg.heatingCurve.heatingCurve = {{20, 25, 30, 35, 45, 55, 65, 75, 80}};
	}

	heating::BoilerController makeController() {
		auto bp = std::make_unique<MockGpioPort>(99);
		boilerGpio = bp.get();
		return heating::BoilerController(cfg, [this]() { return outdoorTemp; }, [](bool, uint8_t) {}, [this](uint8_t temp) { lastSetHeatingTemp = temp; }, std::move(bp), valveMocks.makePorts(8), {});
	}
};

TEST_F(BoilerOnOffOutdoorTest, SetsHeatingTemperatureFromCurve) {
	outdoorTemp = 500; // 5°C → curve gives 35°C
	auto bc = makeController();

	bc.startBoilerOrContinue(true, false, std::nullopt);

	EXPECT_EQ(lastSetHeatingTemp, 35);

	// Also sets boiler port
	EXPECT_TRUE(boilerGpio->lastValue);
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
