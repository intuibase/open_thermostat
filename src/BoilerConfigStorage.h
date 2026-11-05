#pragma once
#include "BoilerConfigValidation.h"
#include <SPIFFS.h>
#include <memory>

namespace config {
template<class Filesystem>
BoilerConfig loadBoilerConfiguration(Filesystem &filesystem) {
	auto file = filesystem.open("/cfg/cfgboiler.json", FILE_READ);
	if (!file) return {};
	auto text = file.readString();
	file.close();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(text.c_str()), &cJSON_Delete);
	BoilerConfig result;
	if (!parseBoilerConfig(root.get(), result)) return {};
	return result;
}
}
