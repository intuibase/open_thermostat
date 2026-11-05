#pragma once
#include "config.h"
#include <cmath>
#include <cstring>

namespace config {
// Shared by boot parsing, REST validation and native regression tests.
inline bool parseBoilerSupply(cJSON *boiler, BoilerConfig &config) {
	config.boiler.supplyMode = BoilerConfig::supplyMode_t::legacy;
	config.boiler.fixedSupplyTemperature.reset();
	config.boiler.weatherFallback = true;
	auto text = [boiler](const char *key) {
		auto item = cJSON_GetObjectItemCaseSensitive(boiler, key);
		return cJSON_IsString(item) ? item->valuestring : "";
	};
	auto mode = text("controlMode");
	if (!strcmp(mode, "onoff")) config.boiler.controlMode = BoilerConfig::controlMode_t::onoff;
	else if (!strcmp(mode, "onoff_outdoor")) config.boiler.controlMode = BoilerConfig::controlMode_t::onoff_outdoor;
	else if (!strcmp(mode, "ems")) config.boiler.controlMode = BoilerConfig::controlMode_t::ems;
	else return false;
	auto sourceValue = cJSON_GetObjectItemCaseSensitive(boiler, "outdoorSensor");
	if (sourceValue && !cJSON_IsString(sourceValue)) return false;
	auto source = text("outdoorSensor");
	if (!strcmp(source, "owm")) config.boiler.outdoorSensor = BoilerConfig::outdoorSensor_t::openweather;
	else if (!strcmp(source, "ems")) config.boiler.outdoorSensor = BoilerConfig::outdoorSensor_t::ems;
	else if (!strcmp(source, "auto")) config.boiler.outdoorSensor = BoilerConfig::outdoorSensor_t::automatic;
	else if (!strcmp(source, "no") || !*source) config.boiler.outdoorSensor = BoilerConfig::outdoorSensor_t::no;
	else return false;
	auto fixed = cJSON_GetObjectItemCaseSensitive(boiler, "fixedSupplyTemperature");
	if (fixed && !cJSON_IsNull(fixed)) {
		if (!cJSON_IsNumber(fixed) || !std::isfinite(fixed->valuedouble) || fixed->valuedouble != fixed->valueint || fixed->valueint < 20 || fixed->valueint > 90) return false;
		config.boiler.fixedSupplyTemperature = fixed->valueint;
	}
	auto fallback = cJSON_GetObjectItemCaseSensitive(boiler, "weatherFallback");
	if (fallback) {
		if (!cJSON_IsBool(fallback)) return false;
		config.boiler.weatherFallback = cJSON_IsTrue(fallback);
	}
	auto supply = text("supplyMode");
	if (!*supply && !cJSON_GetObjectItemCaseSensitive(boiler, "supplyMode")) return true; // legacy, no guessed fallback
	if (!strcmp(supply, "boiler")) config.boiler.supplyMode = BoilerConfig::supplyMode_t::boiler;
	else if (!strcmp(supply, "fixed")) config.boiler.supplyMode = BoilerConfig::supplyMode_t::fixed;
	else if (!strcmp(supply, "curve")) config.boiler.supplyMode = BoilerConfig::supplyMode_t::curve;
	else return false;
	const bool relayOnly = config.boiler.controlMode == BoilerConfig::controlMode_t::onoff;
	const bool boilerSetting = config.boiler.supplyMode == BoilerConfig::supplyMode_t::boiler;
	if (relayOnly != boilerSetting) return false; // No trustworthy original EMS setting is available.
	if (!boilerSetting && (!config.boiler.fixedSupplyTemperature ||
		*config.boiler.fixedSupplyTemperature < config.boiler.minHeatingTemp ||
		*config.boiler.fixedSupplyTemperature > config.boiler.maxHeatingTemp)) return false;
	return true;
}

inline bool parseBoilerConfig(cJSON *root, BoilerConfig &config) {
	config = {};
	if (!cJSON_IsObject(root)) return false;
	auto integer = [](cJSON *obj, const char *key, int min, int max, auto &destination) {
		auto value = cJSON_GetObjectItemCaseSensitive(obj, key);
		if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) || value->valuedouble != value->valueint || value->valueint < min || value->valueint > max) return false;
		destination = value->valueint;
		return true;
	};
	auto boiler = cJSON_GetObjectItemCaseSensitive(root, "boiler");
	if (!cJSON_IsObject(boiler) ||
		!integer(boiler, "valvePreheatingDelay", 0, 65535, config.boiler.valvePreheatingDelay) ||
		!integer(boiler, "minHeatingTemp", 20, 90, config.boiler.minHeatingTemp) ||
		!integer(boiler, "maxHeatingTemp", config.boiler.minHeatingTemp, 90, config.boiler.maxHeatingTemp) ||
		!integer(root, "minHeatingCurveTemp", 0, 90, config.heatingCurve.minHeatingCurveTemp) ||
		!integer(root, "maxHeatingCurveTemp", config.heatingCurve.minHeatingCurveTemp, 90, config.heatingCurve.maxHeatingCurveTemp)) return false;
	auto curve = cJSON_GetObjectItemCaseSensitive(root, "heatingCurve");
	if (!cJSON_IsArray(curve) || cJSON_GetArraySize(curve) != 9) return false;
	for (int i = 0; i < 9; ++i) {
		auto point = cJSON_GetArrayItem(curve, i);
		if (!cJSON_IsNumber(point) || !std::isfinite(point->valuedouble) || point->valuedouble != point->valueint || point->valueint < 0 || point->valueint > 90) return false;
		config.heatingCurve.heatingCurve[i] = point->valueint;
	}
	return parseBoilerSupply(boiler, config);
}
}
