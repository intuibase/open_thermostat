
#include "config.h"
#include "BeaconBleAddress.h"
#include "Logger.h"

#include <SPIFFS.h>
#include <array>



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
	if (!obj || obj->type != cJSON_False && obj->type != cJSON_True) {
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


std::vector<uint8_t> parseValves(cJSON *root) {
	std::vector<uint8_t> retVal;
	auto valves = cJSON_GetObjectItem(root, "valves");
	if (valves && valves->type == cJSON_Array) {
		auto noValves = cJSON_GetArraySize(valves);
		for (auto valve = 0; valve < noValves; ++valve) {
			auto item = cJSON_GetArrayItem(valves, valve);
			if (cJSON_IsNumber(item)) {
				retVal.push_back(item->valueint);
			}
		}
	}
	return retVal;
}

heating::Room::TemperatureSetting parseTemperature(cJSON *obj) {
	heating::Room::TemperatureSetting temp;
	temp.name_ = json::getString(obj, "name");
	temp.timeFrom_ = json::getString(obj, "time_from");
	temp.timeTo_ = json::getString(obj, "time_to");
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

heating::Room parseRoom(cJSON *obj) {
	heating::Room room;
	room.baseTemperature_ = json::getInt(obj, "base_temp");
	room.temperatureMarginUp_ = json::getInt(obj, "temp_margin_up");
	room.temperatureMarginDown_ = json::getInt(obj, "temp_margin_down");
	room.name_ = json::getString(obj, "name");
	room.sensorAddress_ = heating::BLEAddresFromString(json::getString(obj, "sensor"));

	if (cJSON_HasObjectItem(obj, "enabled")) {
		auto item = cJSON_GetObjectItem(obj, "enabled");
		room.enabled_ = cJSON_IsTrue(item);
	}

	room.valves_ = parseValves(obj);

	auto temperatures = cJSON_GetObjectItem(obj, "temperatures");
	if (temperatures && temperatures->type == cJSON_Array) {
		for (auto it = 0; it < cJSON_GetArraySize(temperatures); ++it) {
			auto item = cJSON_GetArrayItem(temperatures, it);
			if (cJSON_IsObject(item)) {
				room.temperatures_.push_back(parseTemperature(item));
			}
		}
	}

	heating::logger.printf("Room '%s' sensor: '" PRiBleAddress "' baseTemp: %d enabled: %d valves: %zu temperatures: %zu valves: ", room.name_.c_str(), PRaBleAddress(room.sensorAddress_), room.baseTemperature_, room.enabled_, room.valves_.size(), room.temperatures_.size());

	for (auto valve : room.valves_) {
		heating::logger.printf("%d ", valve);
	}
	heating::logger.println("");

	return room;
}

}

uint8_t getBoilerPin() {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();
	if (!root) {
		heating::logger.println("Error parsing json");
		return {};
	}
	return json::getInt(root.get(), "boiler_pin");
}

RTCPins getRTCPins() {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	if (!file) {
		return {};
	}
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();

	if (!root) {
		heating::logger.println("Error parsing json");
		return {};
	}

	auto rtcpins = cJSON_GetObjectItem(root.get(), "rtci2c");
	if (!rtcpins || rtcpins->type != cJSON_Array || cJSON_GetArraySize(rtcpins) != 2) {
		heating::logger.println("Error parsing json. rtci2c not an array or size!=2\n");
		return {};
	}

	RTCPins pins;

	auto item = cJSON_GetArrayItem(rtcpins, 0);
	if (!cJSON_IsNumber(item)) {
		heating::logger.printf("rtci2c sda NaN\n");
		return {};
	}
	pins.sda = item->valueint;

	item = cJSON_GetArrayItem(rtcpins, 1);
	if (!cJSON_IsNumber(item)) {
		heating::logger.printf("rtci2c scl NaN\n");
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
		heating::logger.println("Error parsing json");
		return {};
	}

	auto emspins = cJSON_GetObjectItem(root.get(), "ems");
	if (!emspins || emspins->type != cJSON_Array || cJSON_GetArraySize(emspins) != 2) {
		heating::logger.println("Error parsing json. ems not an array or size!=2\n");
		return {};
	}

	EmsPins pins;

	auto item = cJSON_GetArrayItem(emspins, 0);
	if (!cJSON_IsNumber(item)) {
		heating::logger.printf("ems rx NaN\n");
		return {};
	}
	pins.rx = item->valueint;

	item = cJSON_GetArrayItem(emspins, 1);
	if (!cJSON_IsNumber(item)) {
		heating::logger.printf("ems tx NaN\n");
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
		heating::logger.println("Error parsing json");
		return {};
	}

	auto emspins = cJSON_GetObjectItem(root.get(), "ems_forwarder");
	if (!emspins || emspins->type != cJSON_Array || cJSON_GetArraySize(emspins) != 2) {
		heating::logger.println("Error parsing json. ems forwarder not an array or size!=2\n");
		return {};
	}

	EmsForwarderPins pins;

	auto item = cJSON_GetArrayItem(emspins, 0);
	if (!cJSON_IsNumber(item)) {
		heating::logger.printf("ems_forwarder rx NaN\n");
		return {};
	}
	pins.rx = item->valueint;

	item = cJSON_GetArrayItem(emspins, 1);
	if (!cJSON_IsNumber(item)) {
		heating::logger.printf("ems_forwarder tx NaN\n");
		return {};
	}
	pins.tx = item->valueint;
	return {pins};
}

std::array<uint8_t, 8> getValvePins() {
	File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();
	if (!root) {
		heating::logger.println("Error parsing json");
		return {};
	}

	auto valve_pins = cJSON_GetObjectItem(root.get(), "valve_pins");
	if (!valve_pins || valve_pins->type != cJSON_Array) {
		heating::logger.println("Error parsing json. valve_pins");
		return {};
	}

	auto noValves = cJSON_GetArraySize(valve_pins);
	if (noValves != 8) {
		heating::logger.printf("Found %d valves, should be 8\n", noValves);
		return {};
	}

	std::array<uint8_t, 8> returnValue;
	for (auto valve = 0; valve < noValves; ++valve) {
		auto item = cJSON_GetArrayItem(valve_pins, valve);
		if (cJSON_IsNumber(item)) {
			returnValue[valve] = item->valueint;
			//			heating::logger.printf("Valve %d pin: %d\n", valve, item->valueint);
		}
	}
	return returnValue;
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
		heating::logger.printf("Error parsing AP config\n'%s'\n", cfg.c_str());
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
		heating::logger.println("Error parsing wifi config");
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
		heating::logger.printf("Error parsing device config\n'%s'\n", cfg.c_str());
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
		heating::logger.printf("Error parsing network config\n'%s'\n", cfg.c_str());
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
		heating::logger.printf("Error parsing bluetooth config\n'%s'\n", cfg.c_str());
		return {};
	}

	auto bt = cJSON_GetObjectItem(network.get(), "bt");
	if (!bt || bt->type != cJSON_Object) {
		heating::logger.printf("Bluetooth scan config not found\n");
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
		heating::logger.printf("Error parsing Mqtt config\n'%s'\n", cfg.c_str());
		return {};
	}

	auto mqtt = cJSON_GetObjectItem(network.get(), "mqtt");
	if (!mqtt || mqtt->type != cJSON_Object) {
		return {};
	}

	MqttConfig config;
	config.enabled = json::getBool(mqtt, "enabled");
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
		heating::logger.printf("Error parsing openweather config\n'%s'\n", cfg.c_str());
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
		heating::logger.printf("Error parsing boiler config\n'%s'\n", cfg.c_str());
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
		heating::logger.println("Error parsing json");
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

std::vector<heating::Room> getRoomsConfig() {
	File file = SPIFFS.open("/cfg/cfgprogram.json", FILE_READ);
	if (!file) {
		return {};
	}
	auto program = parseProgram(file.readString().c_str());
	file.close();
	if (program.empty()) {
		return {};
	}
	heating::logger.printf("getRoomsConfig '%s'\n", program.c_str());

	return getRoomsConfig(program);

}


std::vector<heating::Room> getRoomsConfig(std::string const &program) {
	std::string filename = "/programs/" + program + ".json";

	heating::logger.printf("Reading config for '%s', exists: %d\n", filename.c_str(), SPIFFS.exists(filename.c_str()));

	if (!SPIFFS.exists(filename.c_str())) {
		filename = "/programs/default.json";
		heating::logger.printf("Program not found. Reading config for '%s', exists: %d\n", filename.c_str(), SPIFFS.exists(filename.c_str()));
	}

	File file = SPIFFS.open(filename.c_str(), FILE_READ);
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> rooms(cJSON_Parse(file.readString().c_str()), &cJSON_Delete);
	file.close();

	if (!rooms) {
		heating::logger.printf("Error parsing json. No rooms: '%s'\n", cJSON_GetErrorPtr());
		return {};
	}

	if (rooms->type != cJSON_Array) {
		return {};
	}

	std::vector<heating::Room> retVal;

	auto noRooms = cJSON_GetArraySize(rooms.get());
	for (auto room = 0; room < noRooms; ++room) {
		auto item = cJSON_GetArrayItem(rooms.get(), room);
		if (cJSON_IsObject(item)) {
			retVal.emplace_back(helper::parseRoom(item));
		}
	}
	return retVal;
}

void readDebugOptions() {
	File file = SPIFFS.open("/cfg/cfgdebug.json", FILE_READ);
	if (!file) {
		heating::logger.printf("Debug options not found. Using defaults\n");
		return;
	}
	heating::logger.printf("Reading debug options from file\n");
	String cfg = file.readString();
	setDebugOptionsFromJson(cfg.c_str());
	file.close();
}

void setDebugOptionsFromJson(const char *json) {
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(json), &cJSON_Delete);
	#define READ_JSON_DEBUG_OPTION(name) debug::debug.name = json::getBool(root.get(), #name)
	READ_JSON_DEBUG_OPTION(debugRoomTemperatures);
	READ_JSON_DEBUG_OPTION(debugAutoLock);
	READ_JSON_DEBUG_OPTION(debugHeatingController);
	READ_JSON_DEBUG_OPTION(debugTemperatureReader);
	READ_JSON_DEBUG_OPTION(debugREST);
	READ_JSON_DEBUG_OPTION(debugOpenWeather);
	READ_JSON_DEBUG_OPTION(debugBoilerController);
	READ_JSON_DEBUG_OPTION(debugEmsBusUart);
	READ_JSON_DEBUG_OPTION(debugEmsBusUartForwarder);
	READ_JSON_DEBUG_OPTION(debugEmsController);
	READ_JSON_DEBUG_OPTION(debugEmsVerbose);
	READ_JSON_DEBUG_OPTION(debugMQTT);
	READ_JSON_DEBUG_OPTION(debugFatal);
}

}
