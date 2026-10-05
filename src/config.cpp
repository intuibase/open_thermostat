#include "config.h"
#include "BeaconBleAddress.h"
#include "Logging.h"
#include "TimeUtils.h"

#include <SPIFFS.h>
#include <algorithm>
#include <memory>
#include <set>
#include <unordered_map>

namespace {
ib::logger::LoggerInterface::LogFeatureType configLogFeature() {
	return heating::logger->addFeature("Config");
}
}

namespace json {
std::string getString(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_String) {
		return {};
	}
	return obj->valuestring;
}

int getInt(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_Number) {
		return {};
	}
	return obj->valueint;
}

bool getBool(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || (obj->type != cJSON_False && obj->type != cJSON_True)) {
		return false;
	}
	return obj->type == cJSON_True;
}

float getFloat(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_Number) {
		return {};
	}
	return obj->valuedouble;
}

}

namespace config {

namespace helper {

std::vector<std::string> parseValves(cJSON *root) {
	std::vector<std::string> retVal;
	auto valves = cJSON_GetObjectItem(root, "valves");
	if (valves && valves->type == cJSON_Array) {
		auto noValves = cJSON_GetArraySize(valves);
		for (auto valve = 0; valve < noValves; ++valve) {
			auto item = cJSON_GetArrayItem(valves, valve);
			if (cJSON_IsString(item)) {
				retVal.push_back(item->valuestring);
			} else if (cJSON_IsNumber(item)) {
				retVal.push_back(std::to_string(item->valueint)); // backward compat: index as string
			}
		}
	}
	return retVal;
}

heating::RoomConfig::TemperatureSetting parseTemperature(cJSON *obj) {
	heating::RoomConfig::TemperatureSetting temp{heating::logger};
	temp.name_ = json::getString(obj, "name");
	try {
		temp.timeFrom_ = ib::timeutils::parseTimeHHMM(json::getString(obj, "time_from"));
		temp.timeTo_ = ib::timeutils::parseTimeHHMM(json::getString(obj, "time_to"));
	} catch (std::exception const &e) {
		DBGLOGFI(heating::logger, configLogFeature(), "Exception parsing temperature '%s' time ranges: %s\n", temp.name_.c_str(), e.what());
	}
	temp.temperature_ = json::getInt(obj, "temp");
	temp.heatingTemperatureOverride_ = json::getOptInt<uint8_t>(obj, "boiler_temp");
	temp.valves_ = parseValves(obj);

	if (cJSON_HasObjectItem(obj, "enabled")) {
		auto item = cJSON_GetObjectItem(obj, "enabled");
		temp.enabled_ = cJSON_IsTrue(item);
	}

	if (cJSON_HasObjectItem(obj, "days")) {
		auto days = cJSON_GetObjectItem(obj, "days");
		if (days && days->type == cJSON_Array) {
			auto noDays = cJSON_GetArraySize(days);
			if (noDays > 0) {
				temp.days_ = {{false, false, false, false, false, false, false}};
			}

			for (auto day = 0; day < noDays; ++day) {
				auto item = cJSON_GetArrayItem(days, day);
				if (cJSON_IsNumber(item)) {
					if (item->valueint >= 0 && item->valueint < 7) {
						temp.days_[item->valueint] = true;
					}
				}
			}
		}
	}

	return temp;
}

heating::RoomConfig parseRoomDefinition(cJSON *obj) {
	heating::RoomConfig room;
	room.name_ = json::getString(obj, "name");
	room.id_ = json::getString(obj, "id");
	room.sensorAddress_ = heating::BLEAddresFromString(json::getString(obj, "sensor"));
	room.valves_ = parseValves(obj);
	return room;
}

void applyRoomProgram(heating::RoomConfig &room, cJSON *obj) {
	room.baseTemperature_ = json::getInt(obj, "base_temp");
	room.temperatureMarginUp_ = json::getInt(obj, "temp_margin_up");
	room.temperatureMarginDown_ = json::getInt(obj, "temp_margin_down");

	if (cJSON_HasObjectItem(obj, "enabled")) {
		auto item = cJSON_GetObjectItem(obj, "enabled");
		room.enabled_ = cJSON_IsTrue(item);
	}

	auto temperatures = cJSON_GetObjectItem(obj, "temperatures");
	if (temperatures && temperatures->type == cJSON_Array) {
		for (auto it = 0; it < cJSON_GetArraySize(temperatures); ++it) {
			auto item = cJSON_GetArrayItem(temperatures, it);
			if (cJSON_IsObject(item)) {
				room.temperatures_.push_back(parseTemperature(item));
			}
		}
	}

}
}

std::optional<PinConfig> getBoilerPin() {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	if (!file)
		return {};
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();
	if (!root)
		return {};

	auto bp = cJSON_GetObjectItem(root.get(), "boiler_pin");
	if (!bp)
		return {};

	// New format: {"source": "builtin", "pin": 14}
	if (bp->type == cJSON_Object) {
		auto src = cJSON_GetObjectItem(bp, "source");
		auto pin = cJSON_GetObjectItem(bp, "pin");
		if (!src || !cJSON_IsString(src) || !pin || !cJSON_IsNumber(pin))
			return {};
		return PinConfig{src->valuestring, static_cast<uint8_t>(pin->valueint)};
	}

	// Legacy format: plain number
	if (cJSON_IsNumber(bp)) {
		return PinConfig{"builtin", static_cast<uint8_t>(bp->valueint)};
	}

	// null = not used
	return {};
}

RTCPins getRTCPins() {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	if (!file) {
		return {};
	}
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();

	if (!root) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing json");
		return {};
	}

	auto rtcpins = cJSON_GetObjectItem(root.get(), "i2c");
	if (!rtcpins || rtcpins->type != cJSON_Array || cJSON_GetArraySize(rtcpins) != 2) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing json. i2c not an array or size!=2\n");
		return {};
	}

	RTCPins pins;

	auto item = cJSON_GetArrayItem(rtcpins, 0);
	if (!cJSON_IsNumber(item)) {
		DBGLOGFI(heating::logger, configLogFeature(), "i2c sda NaN\n");
		return {};
	}
	pins.sda = item->valueint;

	item = cJSON_GetArrayItem(rtcpins, 1);
	if (!cJSON_IsNumber(item)) {
		DBGLOGFI(heating::logger, configLogFeature(), "i2c scl NaN\n");
		return {};
	}
	pins.scl = item->valueint;
	return pins;
}

EmsPins getEmsPins() {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	if (!file) {
		return {};
	}
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();

	if (!root) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing json");
		return {};
	}

	auto emspins = cJSON_GetObjectItem(root.get(), "ems");
	if (!emspins || emspins->type != cJSON_Array || cJSON_GetArraySize(emspins) != 2) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing json. ems not an array or size!=2\n");
		return {};
	}

	EmsPins pins;

	auto item = cJSON_GetArrayItem(emspins, 0);
	if (!cJSON_IsNumber(item)) {
		DBGLOGFI(heating::logger, configLogFeature(), "ems rx NaN\n");
		return {};
	}
	pins.rx = item->valueint;

	item = cJSON_GetArrayItem(emspins, 1);
	if (!cJSON_IsNumber(item)) {
		DBGLOGFI(heating::logger, configLogFeature(), "ems tx NaN\n");
		return {};
	}
	pins.tx = item->valueint;
	return pins;
}

std::optional<EmsForwarderPins> getEmsForwarderPins() {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	if (!file) {
		return {};
	}
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();

	if (!root) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing json");
		return {};
	}

	auto emspins = cJSON_GetObjectItem(root.get(), "ems_forwarder");
	if (!emspins || emspins->type != cJSON_Array || cJSON_GetArraySize(emspins) != 2) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing json. ems forwarder not an array or size!=2\n");
		return {};
	}

	EmsForwarderPins pins;

	auto item = cJSON_GetArrayItem(emspins, 0);
	if (!cJSON_IsNumber(item)) {
		DBGLOGFI(heating::logger, configLogFeature(), "ems_forwarder rx NaN\n");
		return {};
	}
	pins.rx = item->valueint;

	item = cJSON_GetArrayItem(emspins, 1);
	if (!cJSON_IsNumber(item)) {
		DBGLOGFI(heating::logger, configLogFeature(), "ems_forwarder tx NaN\n");
		return {};
	}
	pins.tx = item->valueint;
	return {pins};
}

std::vector<PinConfig> getValvePins() {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	if (!file)
		return {};
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();
	if (!root) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing json");
		return {};
	}

	auto valve_pins = cJSON_GetObjectItem(root.get(), "valve_pins");
	if (!valve_pins || valve_pins->type != cJSON_Array) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing json. valve_pins");
		return {};
	}

	auto noValves = cJSON_GetArraySize(valve_pins);
	std::vector<PinConfig> result;
	result.reserve(noValves);

	for (int i = 0; i < noValves; ++i) {
		auto item = cJSON_GetArrayItem(valve_pins, i);

		if (cJSON_IsNull(item)) {
			continue; // skip "not used" entries
		}

		// New format: {"source": "ext:Ext1", "pin": 0}
		if (item->type == cJSON_Object) {
			auto src = cJSON_GetObjectItem(item, "source");
			auto pin = cJSON_GetObjectItem(item, "pin");
			if (src && cJSON_IsString(src) && pin && cJSON_IsNumber(pin)) {
				std::string label;
				auto lbl = cJSON_GetObjectItem(item, "label");
				if (lbl && cJSON_IsString(lbl))
					label = lbl->valuestring;
				result.push_back(PinConfig{src->valuestring, static_cast<uint8_t>(pin->valueint), std::move(label)});
			}
			continue;
		}

		// Legacy format: plain number
		if (cJSON_IsNumber(item)) {
			result.push_back(PinConfig{"builtin", static_cast<uint8_t>(item->valueint)});
		}
	}
	return result;
}

std::vector<GpioExtenderConfig> getGpioExtenders() {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	if (!file)
		return {};
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();
	if (!root)
		return {};

	auto arr = cJSON_GetObjectItem(root.get(), "gpio_extenders");
	if (!arr || arr->type != cJSON_Array)
		return {};

	std::vector<GpioExtenderConfig> result;
	for (int i = 0; i < cJSON_GetArraySize(arr); ++i) {
		auto item = cJSON_GetArrayItem(arr, i);
		if (item->type != cJSON_Object)
			continue;

		auto type = cJSON_GetObjectItem(item, "type");
		auto addr = cJSON_GetObjectItem(item, "address");
		auto label = cJSON_GetObjectItem(item, "label");

		if (type && addr && label && cJSON_IsString(type) && cJSON_IsNumber(addr) && cJSON_IsString(label)) {
			result.push_back(GpioExtenderConfig{type->valuestring, static_cast<uint8_t>(addr->valueint), label->valuestring});
		}
	}
	return result;
}

APConfig getAPConfig() {
	File file = SPIFFS.open("/cfg/cfgap.json", FILE_READ);
	if (!file) {
		return {};
	}

	auto cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing AP config\n'%s'\n", cfg.c_str());
		return {};
	}

	APConfig config;
	config.ssid = json::getString(network.get(), "ssid");
	config.password = json::getString(network.get(), "password");
	config.channel = json::getInt(network.get(), "channel");
	config.hostname = json::getString(network.get(), "hostname");
	config.ip = json::getString(network.get(), "ip");
	config.gateway = json::getString(network.get(), "gateway");
	config.subnetMask = json::getString(network.get(), "subnetMask");
	config.listenPort = json::getInt(network.get(), "listenPort");
	config.ntpUtcOffset = json::getInt(network.get(), "ntpUtcOffset");
	config.ntpDaylightUtcOffset = json::getInt(network.get(), "ntpDaylightUtcOffset");
	config.timeZone = json::getString(network.get(), "timeZone");

	return config;
}

WiFiConfig getWiFiConfig() {
	File file = SPIFFS.open("/cfg/cfgwifi.json", FILE_READ);
	if (!file) {
		return {};
	}
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing wifi config");
		return {};
	}

	WiFiConfig config;
	config.ssid = json::getString(network.get(), "ssid");
	config.password = json::getString(network.get(), "password");
	return config;
}

EmsConfig getEmsConfig() {
	File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_READ);
	if (!file) {
		return {};
	}

	String cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing device config\n'%s'\n", cfg.c_str());
		return {};
	}


	EmsConfig config;
	config.emsEnabled = json::getBool(network.get(), "emsEnabled");
	config.emsForwarderEnabled = json::getBool(network.get(), "emsForwarderEnabled");
	return config;
}

NetworkConfig getNetworkConfig() {
	File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_READ);
	if (!file) {
		return {};
	}

	String cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing network config\n'%s'\n", cfg.c_str());
		return {};
	}

//TODO getStringOptional and defaults
	NetworkConfig config;
	config.hostname = json::getString(network.get(), "hostname");
	config.listenPort = json::getInt(network.get(), "listenPort");

	config.rtcEnabled = json::getBool(network.get(), "rtcEnabled");

	config.ntpEnabled = json::getBool(network.get(), "ntpEnabled");
	config.ntpHost = json::getString(network.get(), "ntpHost");
	config.ntpUtcOffset = json::getInt(network.get(), "ntpUtcOffset");
	config.ntpDaylightUtcOffset = json::getInt(network.get(), "ntpDaylightUtcOffset");
	config.timeZone = json::getString(network.get(), "timeZone");

	config.loggerEnabled = json::getBool(network.get(), "loggerEnabled");
	config.loggerHost = json::getString(network.get(), "loggerHost");
	config.loggerPort = json::getInt(network.get(), "loggerPort");
	config.loggerTimeoutMs = json::getInt(network.get(), "loggerTimeoutMs");

	return config;
}

BluetoothConfig getBluetoothConfig() {
	File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_READ);
	if (!file) {
		return {};
	}

	String cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing bluetooth config\n'%s'\n", cfg.c_str());
		return {};
	}

	auto bt = cJSON_GetObjectItem(network.get(), "bt");
	if (!bt || bt->type != cJSON_Object) {
		DBGLOGFI(heating::logger, configLogFeature(), "Bluetooth scan config not found\n");
		return {};
	}

	BluetoothConfig config;
	config.scanTime = json::getOptInt<uint16_t>(bt, "scanTime").value_or(60);
	config.scanInterval = json::getOptInt<uint16_t>(bt, "scanInterval").value_or(60);

	return config;
}


MqttConfig getMqttConfig() {
	File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_READ);
	if (!file) {
		return {};
	}

	String cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing Mqtt config\n'%s'\n", cfg.c_str());
		return {};
	}

	auto mqtt = cJSON_GetObjectItem(network.get(), "mqtt");
	if (!mqtt || mqtt->type != cJSON_Object) {
		return {};
	}

	MqttConfig config;
	config.enabled = json::getBool(mqtt, "enabled");
	auto publishDiscovery = cJSON_GetObjectItemCaseSensitive(mqtt, "publishHomeAssistantDiscovery");
	config.publishHomeAssistantDiscovery = publishDiscovery == nullptr || cJSON_IsTrue(publishDiscovery);
	config.brokerAddress = json::getString(mqtt, "brokerAddress");
	config.brokerPort = json::getInt(mqtt, "brokerPort");
	config.username = json::getString(mqtt, "username");
	config.password = json::getString(mqtt, "password");
	config.clientId = json::getString(mqtt, "clientId");
	config.base = json::getString(mqtt, "base");
	config.keepAlive = json::getInt(mqtt, "keepAlive");
	config.interval = json::getInt(mqtt, "interval");

	return config;
}

OpenWeatherConfig getOpenWeatherConfig() {
	File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_READ);
	if (!file) {
		return {};
	}

	String cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> network(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	file.close();

	if (!network || network->type != cJSON_Object) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing openweather config\n'%s'\n", cfg.c_str());
		return {};
	}

	auto openweather = cJSON_GetObjectItem(network.get(), "openweather");
	if (!openweather || openweather->type != cJSON_Object) {
		return {};
	}

	OpenWeatherConfig config;
	config.enabled = json::getBool(openweather, "enabled");
	config.appid = json::getString(openweather, "appid");
	config.latitude = json::getString(openweather, "latitude");
	config.longitude = json::getString(openweather, "longitude");
	config.interval = json::getInt(openweather, "interval");
	return config;
}

BoilerConfig getBoilerConfig() {
	File file = SPIFFS.open("/cfg/cfgboiler.json", FILE_READ);
	if (!file) {
		return {};
	}

	String cfg = file.readString();
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(cfg.c_str()), &cJSON_Delete);
	cfg.clear();
	file.close();

	if (!root.get() || root->type != cJSON_Object) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing boiler config\n'%s'\n", cfg.c_str());
		return {};
	}

	BoilerConfig config;

	config.heatingCurve.minHeatingCurveTemp = json::getInt(root.get(), "minHeatingCurveTemp");
	config.heatingCurve.maxHeatingCurveTemp = json::getInt(root.get(), "maxHeatingCurveTemp");

	auto heatingCurve = cJSON_GetObjectItem(root.get(), "heatingCurve");
	if (!cJSON_IsArray(heatingCurve)) {
		return {};
	}

	auto noPoints = cJSON_GetArraySize(heatingCurve);
	for (auto pt = 0; pt < noPoints; ++pt) {
		auto item = cJSON_GetArrayItem(heatingCurve, pt);
		if (cJSON_IsNumber(item)) {
			config.heatingCurve.heatingCurve[pt] = item->valueint;
		}
	}

	auto boiler = cJSON_GetObjectItem(root.get(), "boiler");
	if (!boiler || boiler->type != cJSON_Object) {
		return {};
	}

	config.boiler.valvePreheatingDelay = json::getInt(boiler, "valvePreheatingDelay");
	config.boiler.minHeatingTemp = json::getInt(boiler, "minHeatingTemp");
	config.boiler.minHeatingTemp = json::getInt(boiler, "maxHeatingTemp");
	auto controlMode = json::getString(boiler, "controlMode");
	if (controlMode == "ems") {
		config.boiler.controlMode = BoilerConfig::controlMode_t::ems;
	} else if (controlMode == "onoff") {
		config.boiler.controlMode = BoilerConfig::controlMode_t::onoff;
	} else if (controlMode == "onoff_outdoor") {
		config.boiler.controlMode = BoilerConfig::controlMode_t::onoff_outdoor;
	}

	auto outdoorSensor = json::getString(boiler, "outdoorSensor");
	if (outdoorSensor == "owm") {
		config.boiler.outdoorSensor = BoilerConfig::outdoorSensor_t::openweather;
	} else if (outdoorSensor == "ems") {
		config.boiler.outdoorSensor = BoilerConfig::outdoorSensor_t::ems;
	} else {
		config.boiler.outdoorSensor = BoilerConfig::outdoorSensor_t::no;
	}

	return config;
}


std::string parseProgram(std::string const &data) {
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(data.c_str()), &cJSON_Delete);
	if (!root) {
		DBGLOGFI(heating::logger, configLogFeature(), "Error parsing json");
		return {};
	}
	return json::getString(root.get(), "program");
}

std::string getCurrentProgram() {
	File file = SPIFFS.open("/cfg/cfgprogram.json", FILE_READ);
	if (!file) {
		return {};
	}
	auto program = parseProgram(file.readString().c_str());
	file.close();
	return program;
}

std::vector<heating::RoomConfig> getRoomsConfig(std::string const &program) {
	File roomFile = SPIFFS.open("/cfg/rooms.json", FILE_READ);
	if (!roomFile) {
		DBGLOGFI(heating::logger, configLogFeature(), "Room catalog /cfg/rooms.json missing; heating disabled\n");
		return {};
	}
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> catalog(cJSON_Parse(roomFile.readString().c_str()), &cJSON_Delete);
	roomFile.close();
	if (!catalog || !cJSON_IsArray(catalog.get())) {
		DBGLOGFI(heating::logger, configLogFeature(), "Invalid room catalog; heating disabled\n");
		return {};
	}
	std::vector<heating::RoomConfig> rooms;
	std::unordered_map<std::string, size_t> roomPositions;
	std::set<std::string> roomNames;
	std::set<std::string> availableValves;
	auto pins = getValvePins();
	for (size_t i = 0; i < pins.size(); ++i) {
		availableValves.insert(std::to_string(i));
		if (!pins[i].label.empty()) availableValves.insert(pins[i].label);
	}
	for (auto i = 0; i < cJSON_GetArraySize(catalog.get()); ++i) {
		auto item = cJSON_GetArrayItem(catalog.get(), i);
		if (!cJSON_IsObject(item)) return {};
		auto id = json::getString(item, "id");
		auto name = json::getString(item, "name");
		if (id.size() != 8 || id.find_first_not_of("0123456789abcdef") != std::string::npos || roomPositions.count(id) || name.empty() || !roomNames.insert(name).second || !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "sensor")) || !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(item, "valves"))) {
			DBGLOGFI(heating::logger, configLogFeature(), "Invalid or duplicate room ID in catalog; heating disabled\n");
			return {};
		}
		std::set<std::string> usedValves;
		auto valves = cJSON_GetObjectItemCaseSensitive(item, "valves");
		for (auto valve = valves->child; valve; valve = valve->next) {
			std::string value;
			if (cJSON_IsString(valve)) value = valve->valuestring;
			else if (cJSON_IsNumber(valve) && valve->valuedouble == valve->valueint && valve->valueint >= 0) value = std::to_string(valve->valueint);
			else return {};
			if (!availableValves.count(value) || !usedValves.insert(value).second) {
				DBGLOGFI(heating::logger, configLogFeature(), "Unknown or duplicate valve in room '%s'; heating disabled\n", name.c_str());
				return {};
			}
		}
		roomPositions.emplace(id, rooms.size());
		rooms.emplace_back(helper::parseRoomDefinition(item));
	}

	std::string filename = "/programs/" + program + ".json";

	DBGLOGFI(heating::logger, configLogFeature(), "Reading config for '%s', exists: %d\n", filename.c_str(), SPIFFS.exists(filename.c_str()));

	if (!SPIFFS.exists(filename.c_str())) {
		filename = "/programs/default.json";
		DBGLOGFI(heating::logger, configLogFeature(), "Program not found. Reading config for '%s', exists: %d\n", filename.c_str(), SPIFFS.exists(filename.c_str()));
	}

	File file = SPIFFS.open(filename.c_str(), FILE_READ);
	if (!file) return rooms;
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> programRooms(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();

	if (!programRooms || !cJSON_IsArray(programRooms.get())) {
		DBGLOGFI(heating::logger, configLogFeature(), "Invalid program '%s'; rooms disabled\n", filename.c_str());
		return rooms;
	}

	std::vector<heating::RoomConfig> configuredRooms = rooms;
	std::unordered_map<std::string, bool> usedRooms;
	for (auto i = 0; i < cJSON_GetArraySize(programRooms.get()); ++i) {
		auto item = cJSON_GetArrayItem(programRooms.get(), i);
		if (!cJSON_IsObject(item)) return rooms;
		if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(item, "base_temp")) || !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(item, "temp_margin_up")) || !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(item, "temp_margin_down"))) return rooms;
		auto id = json::getString(item, "room_id");
		auto found = roomPositions.find(id);
		if (found == roomPositions.end() || usedRooms.count(id)) {
			DBGLOGFI(heating::logger, configLogFeature(), "Unknown or duplicate room ID in program '%s'; rooms disabled\n", filename.c_str());
			return rooms;
		}
		usedRooms.emplace(id, true);
		auto &room = configuredRooms[found->second];
		helper::applyRoomProgram(room, item);
		for (auto const &setting : room.temperatures_) {
			for (auto const &valve : setting.valves_) {
				if (std::find(room.valves_.begin(), room.valves_.end(), valve) == room.valves_.end()) {
					DBGLOGFI(heating::logger, configLogFeature(), "Override valve outside room '%s'; rooms disabled\n", room.name_.c_str());
					return rooms;
				}
			}
		}
	}
	return configuredRooms;
}

void readDebugOptions() {
	File file = SPIFFS.open("/cfg/cfgdebug.json", FILE_READ);
	if (!file) {
		DBGLOGFI(heating::logger, configLogFeature(), "Debug options not found. Using defaults\n");
		return;
	}
	DBGLOGFI(heating::logger, configLogFeature(), "Reading debug options from file\n");
	String cfg = file.readString();
	setDebugOptionsFromJson(cfg.c_str());
	file.close();
}

void setDebugOptionsFromJson(const char *json) {
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(json), &cJSON_Delete);
	if (!cJSON_IsObject(root.get())) return;
	std::unordered_map<std::string, bool> settings;
	for (auto *item = root->child; item; item = item->next) {
		if (item->string && cJSON_IsBool(item)) {
			settings.emplace(item->string, cJSON_IsTrue(item));
		}
	}
	if (!heating::logger) return;
	for (auto const &[id, name] : heating::logger->getRegisteredFeatures()) {
		auto setting = settings.find(name);
		if (setting != settings.end()) heating::logger->enableFeature(id, setting->second);
	}
}

}
