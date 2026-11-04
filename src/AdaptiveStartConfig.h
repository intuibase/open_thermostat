#pragma once
#include <cJSON.h>
#include <cmath>
#include <initializer_list>
#include <cstring>

namespace heating::aos {
inline bool validConfigJson(cJSON *room) {
	auto config = cJSON_GetObjectItemCaseSensitive(room, "adaptive_start");
	if (!config)
		return true;
	if (!cJSON_IsObject(config))
		return false;
	for (const char *key : {"thermalLearningEnabled", "optimalStartEnabled", "shadowMode"}) {
		auto value = cJSON_GetObjectItemCaseSensitive(config, key);
		if (value && !cJSON_IsBool(value))
			return false;
	}
	for (const char *key : {"maxLeadMinutes", "marginMinutes"}) {
		auto value = cJSON_GetObjectItemCaseSensitive(config, key);
		if (!value)
			continue;
		const int minimum = std::strcmp(key, "maxLeadMinutes") == 0 ? 60 : 0;
		const int maximum = minimum ? 720 : 60;
		if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) || value->valuedouble != value->valueint || value->valueint < minimum || value->valueint > maximum)
			return false;
	}
	return true;
}
} // namespace heating::aos
