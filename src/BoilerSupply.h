#pragma once

#include "config.h"
#include "HeatingCurve.h"
#include <algorithm>

namespace heating {

inline std::optional<int16_t> freshOutdoor(std::optional<int16_t> temperature, uint64_t updated,
	uint64_t now, uint64_t maxAge) {
	if (!updated || now < updated || now - updated > maxAge || !temperature || *temperature < -6000 || *temperature > 6000) return {};
	return temperature;
}

struct OutdoorReading {
	std::optional<int16_t> temperature; // hundredths of °C; inputs must already be fresh
	const char *source = "missing";
	const char *quality = "missing_stale_or_invalid";
};

inline OutdoorReading selectOutdoor(const config::BoilerConfig &config,
	std::optional<int16_t> ems, std::optional<int16_t> weather) {
	auto valid = [](std::optional<int16_t> t) { return t && *t >= -6000 && *t <= 6000 ? t : std::nullopt; };
	ems = valid(ems); weather = valid(weather);
	using Source = config::BoilerConfig::outdoorSensor_t;
	const auto source = config.boiler.outdoorSensor;
	// Historical "no" meant OpenWeather first, with EMS as a secondary source.
	const bool emsFirst = source == Source::ems || source == Source::automatic;
	const auto first = emsFirst ? ems : weather;
	const auto second = emsFirst ? weather : ems;
	if (first) return {first, emsFirst ? "ems" : "openweather", "fresh"};
	if ((config.boiler.weatherFallback || source == Source::automatic) && second)
		return {second, emsFirst ? "openweather" : "ems", "fresh_secondary"};
	return {};
}

inline config::BoilerConfig::supplyMode_t supplyMode(const config::BoilerConfig &config) {
	using Mode = config::BoilerConfig::supplyMode_t;
	if (config.boiler.supplyMode != Mode::legacy) return config.boiler.supplyMode;
	return config.boiler.controlMode == config::BoilerConfig::controlMode_t::onoff ? Mode::boiler : Mode::curve;
}

struct SupplyDecision {
	OutdoorReading outdoor;
	std::optional<uint8_t> base, requested; // exact whole degrees sent on EMS
	const char *reason = "boiler_setting";
	bool boosted = false;
};

inline SupplyDecision resolveSupply(const config::BoilerConfig &config, OutdoorReading outdoor,
	std::optional<uint8_t> overrideTemperature = {}) {
	SupplyDecision result;
	if (outdoor.temperature && (*outdoor.temperature < -6000 || *outdoor.temperature > 6000))
		outdoor = {{}, "missing", "invalid"};
	result.outdoor = outdoor;
	using Mode = config::BoilerConfig::supplyMode_t;
	const auto mode = supplyMode(config);
	if (mode == Mode::boiler) return result; // Ordinary relay never writes temperature.
	const int minimum = config.boiler.minHeatingTemp;
	const int maximum = std::max<int>(minimum, config.boiler.maxHeatingTemp);
	if (mode == Mode::curve && outdoor.temperature) {
		result.base = std::clamp<int>(calcHeatingTemperature(*outdoor.temperature, config.heatingCurve.heatingCurve) / 100, minimum, maximum);
		result.reason = "curve";
	} else if (config.boiler.fixedSupplyTemperature) {
		result.base = std::clamp<int>(*config.boiler.fixedSupplyTemperature, minimum, maximum);
		result.reason = mode == Mode::fixed ? "fixed" : "weather_unavailable_fixed_fallback";
	} else {
		result.reason = "configuration_required";
		return result;
	}
	result.requested = result.base;
	if (overrideTemperature) result.requested = std::max<int>(*result.base, std::clamp<int>(*overrideTemperature, minimum, maximum));
	result.boosted = result.requested > result.base;
	return result;
}

} // namespace heating
