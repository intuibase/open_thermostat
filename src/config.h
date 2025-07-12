#pragma once

#include <array>
#include <cJSON.h>
#include <optional>

#include "Room.h"

#define VALVE_COUNT 8

namespace json {

std::string getString(cJSON *root, const char *name);
int getInt(cJSON *root, const char *name);
bool getBool(cJSON *root, const char *name);
float getFloat(cJSON *root, const char *name);

template <typename T>
std::optional<T> getOptInt(cJSON *root, const char *name) {
	auto obj = cJSON_GetObjectItem(root, name);
	if (!obj || obj->type != cJSON_Number) {
		return {};
	}
	return {obj->valueint};
}

}

namespace config {

using valves_t = std::array<uint8_t, VALVE_COUNT>;

valves_t getValvePins();

struct APConfig {
	std::string ssid = "IntuiBaseHeating";
	std::string password = "IntuiBaseHeating";
	uint8_t channel = 10;
	std::string hostname = "heating";
	std::string ip = "10.10.10.1";
	std::string gateway = "10.10.10.1";
	std::string subnetMask = "255.255.255.0";
	uint32_t listenPort = 80;
	uint32_t ntpUtcOffset;
	uint32_t ntpDaylightUtcOffset;
	std::string timeZone;
};

struct EmsConfig {
	bool emsEnabled = false;
	bool emsForwarderEnabled = false;
};

struct NetworkConfig {
	std::string hostname;
	uint32_t listenPort;
	bool rtcEnabled = false;
	bool ntpEnabled = false;
	std::string ntpHost;
	uint32_t ntpUtcOffset;
	uint32_t ntpDaylightUtcOffset;
	std::string timeZone;
	bool loggerEnabled = false;
	std::string loggerHost;
	uint32_t loggerPort;
	uint32_t loggerTimeoutMs;
};

struct MqttConfig {
	bool enabled = false;
	std::string brokerAddress;
	uint16_t brokerPort;
	std::string username;
	std::string password;
	std::string clientId;
	std::string base;
	uint16_t keepAlive = 60;
	uint16_t interval = 10;
};

struct OpenWeatherConfig {
	bool enabled = false;
	std::string appid;
	std::string latitude;
	std::string longitude;
	uint16_t interval;
};

struct BluetoothConfig {
	uint16_t scanTime = 60;
	uint16_t scanInterval = 60;
};

struct BoilerConfig {
	struct HeatingCurve {
		uint8_t minHeatingCurveTemp = 20;
		uint8_t maxHeatingCurveTemp	= 90;
		std::array<uint8_t, 9> heatingCurve;
	} heatingCurve;

	enum class controlMode_t : uint8_t { onoff, onoff_outdoor, ems };
	enum class outdoorSensor_t : uint8_t { openweather, ems, no };

	struct Boiler {
		uint16_t valvePreheatingDelay = 0;
		uint8_t minHeatingTemp = 20;
		uint8_t maxHeatingTemp = 90;
		controlMode_t controlMode = controlMode_t::onoff;
		outdoorSensor_t	outdoorSensor = outdoorSensor_t::no;
	} boiler;
};

struct WiFiConfig {
	std::string ssid;
	std::string password;
};

struct RTCPins {
	uint8_t sda = 18;
	uint8_t scl = 19;
};

struct EmsPins {
	uint8_t rx = 16;
	uint8_t tx = 17;
};

struct EmsForwarderPins {
	uint8_t rx = 13;
	uint8_t tx = 5;
};


WiFiConfig getWiFiConfig();
APConfig getAPConfig();
NetworkConfig getNetworkConfig();
MqttConfig getMqttConfig();
OpenWeatherConfig getOpenWeatherConfig();
BoilerConfig getBoilerConfig();
BluetoothConfig getBluetoothConfig();

uint8_t getBoilerPin();
RTCPins getRTCPins();
EmsPins getEmsPins();
std::optional<EmsForwarderPins> getEmsForwarderPins();
EmsConfig getEmsConfig();

std::vector<heating::Room> getRoomsConfig(std::string const &program);
std::vector<heating::Room> getRoomsConfig();
std::string getCurrentProgram();

std::string parseProgram(std::string const &data);

void setDebugOptionsFromJson(const char *json);
void readDebugOptions();


}
