#include <gtest/gtest.h>
#include "BoilerSupply.h"
#include "BoilerConfigValidation.h"

namespace {
config::BoilerConfig curveConfig() {
	config::BoilerConfig c;
	c.boiler.controlMode = config::BoilerConfig::controlMode_t::ems;
	c.boiler.supplyMode = config::BoilerConfig::supplyMode_t::curve;
	c.boiler.outdoorSensor = config::BoilerConfig::outdoorSensor_t::ems;
	c.boiler.weatherFallback = false;
	c.boiler.fixedSupplyTemperature = 48;
	c.heatingCurve.heatingCurve = {20,25,30,35,45,55,65,75,80};
	return c;
}
TEST(BoilerSupply, FreshnessBoundariesAndInvalidReadings) {
	EXPECT_EQ(heating::freshOutdoor(-2000, 100, 1800100, 1800000), -2000);
	EXPECT_FALSE(heating::freshOutdoor(500, 100, 1800101, 1800000));
	EXPECT_FALSE(heating::freshOutdoor(500, 0, 100, 1800000));
	EXPECT_FALSE(heating::freshOutdoor(500, 200, 100, 1800000));
	EXPECT_FALSE(heating::freshOutdoor(6001, 100, 200, 1800000));
	EXPECT_FALSE(heating::freshOutdoor({}, 100, 200, 1800000));
}
TEST(BoilerSupply, ExplicitSourceAutomaticAndSecondaryFallback) {
	auto c = curveConfig();
	EXPECT_EQ(heating::selectOutdoor(c, 500, 1000).temperature, 500);
	EXPECT_FALSE(heating::selectOutdoor(c, {}, 1000).temperature);
	c.boiler.weatherFallback = true;
	EXPECT_EQ(heating::selectOutdoor(c, {}, 1000).temperature, 1000);
	c.boiler.outdoorSensor = config::BoilerConfig::outdoorSensor_t::openweather;
	EXPECT_EQ(heating::selectOutdoor(c, 500, 1000).temperature, 1000);
	EXPECT_EQ(heating::selectOutdoor(c, 500, {}).temperature, 500);
	c.boiler.weatherFallback = false;
	EXPECT_FALSE(heating::selectOutdoor(c, 500, {}).temperature);
	c.boiler.outdoorSensor = config::BoilerConfig::outdoorSensor_t::automatic;
	EXPECT_EQ(heating::selectOutdoor(c, 500, 1000).temperature, 500);
	EXPECT_EQ(heating::selectOutdoor(c, {}, 1000).temperature, 1000);
	EXPECT_FALSE(heating::selectOutdoor(c, {}, {}).temperature);
}
TEST(BoilerSupply, InvalidIsMissingAndMinusTwentyIsReal) {
	auto c = curveConfig();
	EXPECT_FALSE(heating::selectOutdoor(c, 6001, {}).temperature);
	EXPECT_EQ(heating::resolveSupply(c, {7000,"callback","fresh"}).base, 48);
	EXPECT_EQ(heating::selectOutdoor(c, -2000, {}).temperature, -2000);
	EXPECT_EQ(heating::resolveSupply(c, heating::selectOutdoor(c, -2000, {})).base, 80);
	EXPECT_EQ(heating::resolveSupply(c, {}).base, 48);
	EXPECT_STREQ(heating::resolveSupply(c, {}).reason, "weather_unavailable_fixed_fallback");
	EXPECT_EQ(heating::resolveSupply(c, heating::selectOutdoor(c, 500, {})).base, 35);
}
TEST(BoilerSupply, LimitsRoundingOverridesAndFixedWithoutWeather) {
	auto c = curveConfig();
	c.boiler.maxHeatingTemp = 70;
	auto outdoor = heating::selectOutdoor(c, 475, {});
	auto decision = heating::resolveSupply(c, outdoor, 35);
	EXPECT_EQ(decision.base, 35); // 35.5 truncated exactly as EMS writes
	EXPECT_FALSE(decision.boosted);
	EXPECT_FALSE(heating::resolveSupply(c, outdoor, 30).boosted);
	decision = heating::resolveSupply(c, outdoor, 90);
	EXPECT_EQ(decision.requested, 70);
	EXPECT_TRUE(decision.boosted);
	c.boiler.supplyMode = config::BoilerConfig::supplyMode_t::fixed;
	EXPECT_EQ(heating::resolveSupply(c, {}).base, 48);
	EXPECT_STREQ(heating::resolveSupply(c, {}).reason, "fixed");
	c.boiler.fixedSupplyTemperature.reset();
	EXPECT_FALSE(heating::resolveSupply(c, {}).base);
	EXPECT_STREQ(heating::resolveSupply(c, {}).reason, "configuration_required");
}
TEST(BoilerSupply, MigrationKeepsTransportAndNeverInventsFallback) {
	for (const auto mode : {"onoff", "onoff_outdoor", "ems"}) {
		auto json = std::string("{\"controlMode\":\"") + mode + "\",\"outdoorSensor\":\"no\"}";
		auto root = cJSON_Parse(json.c_str());
		config::BoilerConfig c;
		ASSERT_TRUE(config::parseBoilerSupply(root, c));
		EXPECT_FALSE(c.boiler.fixedSupplyTemperature);
		EXPECT_EQ(heating::selectOutdoor(c, 500, 1000).temperature, 1000);
		EXPECT_EQ(heating::selectOutdoor(c, 500, {}).temperature, 500);
		EXPECT_EQ(heating::supplyMode(c), std::string(mode) == "onoff" ? config::BoilerConfig::supplyMode_t::boiler : config::BoilerConfig::supplyMode_t::curve);
		cJSON_Delete(root);
	}
}
TEST(BoilerSupply, RejectsUnsupportedCombinationsAndUnknownBaseline) {
	for (const auto json : {
		R"({"controlMode":"ems","supplyMode":"boiler"})",
		R"({"controlMode":"onoff","supplyMode":"fixed","fixedSupplyTemperature":50})",
		R"({"controlMode":"ems","supplyMode":"curve"})",
		R"({"controlMode":"ems","supplyMode":"fixed","fixedSupplyTemperature":50.5})",
		R"({"controlMode":"ems","supplyMode":"fixed","fixedSupplyTemperature":300})",
		R"({"controlMode":"ems","supplyMode":"fixed","fixedSupplyTemperature":50,"weatherFallback":"true"})",
		R"({"controlMode":"ems","supplyMode":"invalid"})",
		R"({"controlMode":"invalid"})",
		R"({"controlMode":"ems","outdoorSensor":"invalid"})"}) {
		auto root = cJSON_Parse(json);
		config::BoilerConfig c;
		EXPECT_FALSE(config::parseBoilerSupply(root, c)) << json;
		cJSON_Delete(root);
	}
	for (int boot = 0; boot < 2; ++boot) {
		auto root = cJSON_Parse(R"({"controlMode":"ems","supplyMode":"fixed","fixedSupplyTemperature":50,"outdoorSensor":"auto","weatherFallback":false})");
		config::BoilerConfig c;
		ASSERT_TRUE(config::parseBoilerSupply(root, c));
		EXPECT_EQ(heating::resolveSupply(c, {}).base, 50);
		cJSON_Delete(root);
	}
}
TEST(BoilerSupply, CompleteConfigValidationAndRoundTrip) {
	const char *saved = R"({"minHeatingCurveTemp":20,"maxHeatingCurveTemp":75,"heatingCurve":[20,25,30,35,45,55,65,75,80],"boiler":{"valvePreheatingDelay":30,"minHeatingTemp":20,"maxHeatingTemp":75,"controlMode":"ems","outdoorSensor":"owm","supplyMode":"curve","fixedSupplyTemperature":48,"weatherFallback":false}})";
	auto root = cJSON_Parse(saved);
	config::BoilerConfig first;
	ASSERT_TRUE(config::parseBoilerConfig(root, first));
	auto serialized = cJSON_PrintUnformatted(root);
	auto reloaded = cJSON_Parse(serialized);
	config::BoilerConfig second;
	ASSERT_TRUE(config::parseBoilerConfig(reloaded, second));
	EXPECT_EQ(second.boiler.maxHeatingTemp, 75);
	EXPECT_EQ(second.boiler.minHeatingTemp, 20);
	EXPECT_EQ(second.boiler.valvePreheatingDelay, 30);
	EXPECT_FALSE(second.boiler.weatherFallback);
	EXPECT_EQ(heating::resolveSupply(first, {}).base, heating::resolveSupply(second, {}).base);
	cJSON_free(serialized);
	cJSON_Delete(reloaded);
	cJSON_DeleteItemFromArray(cJSON_GetObjectItem(root, "heatingCurve"), 0);
	EXPECT_FALSE(config::parseBoilerConfig(root, second));
	cJSON_AddItemToArray(cJSON_GetObjectItem(root, "heatingCurve"), cJSON_CreateNumber(50));
	EXPECT_TRUE(config::parseBoilerConfig(root, second));
	cJSON_AddItemToArray(cJSON_GetObjectItem(root, "heatingCurve"), cJSON_CreateNumber(50));
	EXPECT_FALSE(config::parseBoilerConfig(root, second));
	cJSON_Delete(root);
	for (const auto key : {"valvePreheatingDelay", "minHeatingTemp", "maxHeatingTemp"}) {
		root = cJSON_Parse(saved);
		cJSON_SetNumberValue(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "boiler"), key), -1);
		config::BoilerConfig c;
		EXPECT_FALSE(config::parseBoilerConfig(root, c));
		cJSON_Delete(root);
	}
}

}
