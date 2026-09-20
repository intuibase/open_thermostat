#include "Logging.h"
#include <Arduino.h>
#include <Wire.h>
#include <time.h>

#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <TimeHelpers.h>
#include <logger/Logger.h>
#include <logger/LoggerSerialSink.h>
#include <logger/LoggerSocketSink.h>

#include "config.h"
#include "HeatingController.h"
#include "REST.h"
#include "RTCTimeHelpers.h"

#define ONBOARD_LED 2

namespace {
ib::logger::LoggerInterface::LogFeatureType appLogFeature() {
	static const auto id = heating::logger->addFeature("App");
	return id;
}
}


namespace heating {

HeatingController *controller = nullptr;
std::shared_ptr<ib::logger::LoggerInterface> logger;

std::unique_ptr<REST> rest;

bool wifiAPMode = false;
}

void WiFiGotIP(arduino_event_id_t event, arduino_event_info_t info) {
	DBGLOGFI(heating::logger, appLogFeature(), "WiFI IP address %s hostname: %s\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(), WiFi.getHostname());

	if (WiFi.isConnected()) {
		if (heating::rest) {
			heating::rest->restart();
		}

		auto networkConfig = config::getNetworkConfig();
		if (networkConfig.ntpEnabled) {
			if (!networkConfig.timeZone.empty()) {
				DBGLOGFI(heating::logger, appLogFeature(), "Configuring timezone time '%s' server: '%s' ", networkConfig.timeZone.c_str(), networkConfig.ntpHost.c_str());
				configTzTime(networkConfig.timeZone.c_str(), networkConfig.ntpHost.c_str());
			} else {
				configTime(networkConfig.ntpUtcOffset, networkConfig.ntpDaylightUtcOffset, networkConfig.ntpHost.c_str());
			}
		} else {
			DBGLOGFI(heating::logger, appLogFeature(), "NTP disabled\n");
		}

		struct tm timeinfo;
		if (!getLocalTime(&timeinfo)) {
			DBGLOGFI(heating::logger, appLogFeature(), "Failed to obtain network time. Setting up from RTC\n");
			heating::setLocalTimeFromRTC(networkConfig.timeZone, networkConfig.ntpUtcOffset, networkConfig.ntpDaylightUtcOffset);
		} else if (networkConfig.ntpEnabled) {
			DBGLOGFI(heating::logger, appLogFeature(), "%s", asctime(&timeinfo));
			DBGLOGFI(heating::logger, appLogFeature(), "\nObtained network time, setting up RTC\n");
			heating::setRTCfromLocalTime();
		}

		if (!MDNS.begin(WiFi.getHostname())) { //CONFIG_MDNS_TASK_STACK_SIZE 4096
			DBGLOGFI(heating::logger, appLogFeature(), "Error starting mDNS\n");
		} else {
			MDNS.addService(networkConfig.hostname.c_str(), "http", networkConfig.listenPort);
		}
	}

}

void WifiSetUp(config::WiFiConfig const &wifiConfig, config::NetworkConfig const &networkConfig, config::APConfig const &apConfig, bool startAP) {
	if (startAP) {
		DBGLOGFI(heating::logger, appLogFeature(), "Starting Access Point\n");
		if (heating::controller) {
			heating::controller->stopBluetoothScan();
			heating::controller->waitUntilBluetoothScanFinishAndDeinitBLE();
			DBGLOGFI(heating::logger, appLogFeature(), "Starting Access Point. Stopped bluetooth scan\n");
		}

		WiFi.mode(WIFI_AP);
		WiFi.setSleep(WIFI_PS_NONE);

		bool softAPResult = WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel);

		DBGLOGFI(heating::logger, appLogFeature(), "Soft AP start: %d\n", softAPResult);

		WiFi.onEvent(
			[apConfig](arduino_event_id_t event, arduino_event_info_t info) {
				IPAddress ip, gateway, subnetMask;
				ip.fromString(apConfig.ip.c_str());
				gateway.fromString(apConfig.gateway.c_str());
				subnetMask.fromString(apConfig.subnetMask.c_str());

				bool configResult = WiFi.softAPConfig(ip, gateway, subnetMask);
				WiFi.softAPsetHostname(apConfig.hostname.c_str());

				DBGLOGFI(heating::logger, appLogFeature(), "Started Access Point. Hostname: '%s'. IP address: %s. config: %d\n", WiFi.softAPgetHostname(), WiFi.softAPIP().toString().c_str(), configResult);

				heating::setLocalTimeFromRTC(apConfig.timeZone, apConfig.ntpUtcOffset, apConfig.ntpDaylightUtcOffset);

				if (heating::rest) {
					DBGLOGFI(heating::logger, appLogFeature(), "Restarting REST service\n");
					heating::rest->restart();
				}
				heating::wifiAPMode = true;

				if (!MDNS.begin("heating")) {
					DBGLOGFI(heating::logger, appLogFeature(), "Error starting mDNS\n");
				} else {
					MDNS.addService(apConfig.hostname.c_str(), "http", apConfig.listenPort);
				}
			}, arduino_event_id_t::ARDUINO_EVENT_WIFI_AP_START);



		WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) { DBGLOGFI(heating::logger, appLogFeature(), "AccessPoint client IP assigned: '%s'\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str()); }, arduino_event_id_t::ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);

		WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) { DBGLOGFI(heating::logger, appLogFeature(), "AccessPoint client connected MAC: " MACSTR "\n",MAC2STR(info.wifi_ap_staconnected.mac)); }, arduino_event_id_t::ARDUINO_EVENT_WIFI_AP_STACONNECTED);

	} else {
		DBGLOGFI(heating::logger, appLogFeature(), "networkConfig\n"
							   "  host: %s\n"
							   "  ssid: %s\n", networkConfig.hostname.c_str(), wifiConfig.ssid.c_str());

		heating::wifiAPMode = false;

		const char *pass = nullptr;
		if (!wifiConfig.password.empty()) {
			pass = wifiConfig.password.c_str();
		}

		WiFi.setHostname(networkConfig.hostname.c_str());
		WiFi.begin(wifiConfig.ssid.c_str(), pass);

		WiFi.setHostname(networkConfig.hostname.c_str());
		WiFi.setAutoConnect(true);
		WiFi.setAutoReconnect(true);

		WiFi.onEvent(WiFiGotIP, arduino_event_id_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
	}

	if (networkConfig.loggerEnabled && !networkConfig.loggerHost.empty()) {
		heating::logger->attachSink(std::make_shared<ib::logger::LoggerSocketSink>(
		ib::logger::LoggerInterface::LogLevel::TRACE, networkConfig.loggerHost, networkConfig.loggerPort));
	}
}

void setup() {
	delay(2500); // wait for monitor
	Serial.begin(115200);
	heating::logger = std::make_shared<ib::logger::Logger>(
		std::vector<std::shared_ptr<ib::logger::LoggerSinkInterface>>{
			std::make_shared<ib::logger::LoggerSerialSink>(ib::logger::LoggerInterface::LogLevel::TRACE)});
	DBGLOGFI(heating::logger, appLogFeature(), "Free memory %d/%d (minimum was: %d) MaxAlloc: %d STARTUP\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	pinMode(ONBOARD_LED, OUTPUT);
	digitalWrite(ONBOARD_LED, HIGH);
	digitalWrite(ONBOARD_LED, LOW);

	delay(500);

	const esp_app_desc_t *app = esp_ota_get_app_description();
	const esp_partition_t *partition = esp_ota_get_running_partition();

	DBGLOGFI(heating::logger, appLogFeature(), "Project: %s, version: %s\n", app->project_name, app->version);
	DBGLOGFI(heating::logger, appLogFeature(), "Build: %s %s\n", app->date, app->time);
	DBGLOGFI(heating::logger, appLogFeature(), "IDF: %s\n", app->idf_ver);
	DBGLOGFI(heating::logger, appLogFeature(), "Firmware sha256: %s\n", app->app_elf_sha256);
	DBGLOGFI(heating::logger, appLogFeature(), "Partition: %s, size: %d, encrypted: %d\n", partition->label, partition->size, partition->encrypted);
	DBGLOGFI(heating::logger, appLogFeature(), "-----------------------");
	DBGLOGFI(heating::logger, appLogFeature(), "Starting up");

#ifdef CONFIG_BT_CLASSIC_ENABLED
	DBGLOGFI(heating::logger, appLogFeature(), "CLASSIC BT ENABLED\n");
#else
	DBGLOGFI(heating::logger, appLogFeature(), "BLE BT ENABLED\n");
#endif


#if SOC_UART_NUM > 1
	DBGLOGFI(heating::logger, appLogFeature(), "SERIAL 1 ENABLED\n");
#endif

	if (!SPIFFS.begin(false)) {
		DBGLOGFI(heating::logger, appLogFeature(), "An Error has occurred while mounting SPIFFS");
	}

	config::readDebugOptions();

	DBGLOGFI(heating::logger, appLogFeature(), "Free memory %d/%d (minimum was: %d) MaxAlloc: %d SPIFFS\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	auto networkConfig = config::getNetworkConfig();

	{
		auto rtcpins = config::getRTCPins();
		DBGLOGFI(heating::logger, appLogFeature(), "Configuring i2c on sda %d scl %d\n", rtcpins.sda, rtcpins.scl);
		Wire.begin(rtcpins.sda, rtcpins.scl);
	}
	if (networkConfig.rtcEnabled) {
		heating::startRTC(networkConfig.rtcEnabled);
	} else {
		DBGLOGFI(heating::logger, appLogFeature(), "RTC battery clock disabled\n");
	}

	DBGLOGFI(heating::logger, appLogFeature(), "Free memory %d/%d (minimum was: %d) MaxAlloc: %d RTC\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	auto wifiConfig = config::getWiFiConfig();
	auto apConfig = config::getAPConfig();

	bool startAP = wifiConfig.ssid.empty() /*|| TODO PUSHBUTTON PRESSED */;

	heating::controller = new heating::HeatingController(heating::logger);
	heating::rest = std::make_unique<heating::REST>(heating::logger, *heating::controller, startAP ? apConfig.listenPort : networkConfig.listenPort);
	config::readDebugOptions();

	DBGLOGFI(heating::logger, appLogFeature(), "Free memory %d/%d (minimum was: %d) MaxAlloc: %d STUFF\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	WifiSetUp(wifiConfig, networkConfig, apConfig, startAP);
	DBGLOGFI(heating::logger, appLogFeature(), "Free memory %d/%d (minimum was: %d) MaxAlloc: %d WIFI\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
	config::readDebugOptions();
}

void loop() {
	static unsigned long lastMillis = 0;

	auto now = millis();
	if (ib::millisDurationPassed(now, lastMillis, 10000)) {
		lastMillis = now;

		heating::controller->operate();
		DBGLOGFI(heating::logger, appLogFeature(), "Free memory %d/%d (minimum was: %d) MaxAlloc: %d MinPeekStack: %d boxTemp: %f, UpTime: %lds SPIFFS: %zu/%zu\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), uxTaskGetStackHighWaterMark(nullptr), heating::rtcGetTemp(), esp_timer_get_time()/1000000, SPIFFS.usedBytes(), SPIFFS.totalBytes());

		if (!WiFi.isConnected()) {
			DBGLOGFI(heating::logger, appLogFeature(), "WiFi not connected. Reconnecting.\n");
			WiFi.reconnect();
		} else {
			DBGLOGFI(heating::logger, appLogFeature(), "WiFi IP Address: %s\n", WiFi.localIP().toString().c_str());
		}
	}

	heating::controller->loop();

	if (heating::wifiAPMode || WiFi.isConnected()) {
		heating::rest->handle();
	}
}
