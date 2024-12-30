#include <algorithm>
#include <vector>
#include <set>

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <SPIFFS.h>

#include <SPI.h>  // for I2C with RTC module
#include <RTClib.h>

#include <time.h>


#include "config.h"
#include "HeatingController.h"
#include "Logger.h"
#include "REST.h"
#include "TimeHelpers.h"

#include <ESPmDNS.h>
#include "MQTT.h"

#define ONBOARD_LED 2


namespace debug {
	struct debug debug;
}

namespace heating {

heating::HeatingController *controller = nullptr;
heating::Logger logger;

std::unique_ptr<REST> rest;

bool wifiAPMode = false;
}

void WiFiGotIP(arduino_event_id_t event, arduino_event_info_t info) {
	heating::logger.printf("WiFI IP address %s hostname: %s\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(), WiFi.getHostname());

	if (WiFi.isConnected()) {
		if (heating::rest) {
			heating::rest->restart();
		}

		auto networkConfig = config::getNetworkConfig();
		if (networkConfig.ntpEnabled) {
			if (!networkConfig.timeZone.empty()) {
				heating::logger.printf("Configuring timezone time '%s' server: '%s' ", networkConfig.timeZone.c_str(), networkConfig.ntpHost.c_str());
				configTzTime(networkConfig.timeZone.c_str(), networkConfig.ntpHost.c_str());
			} else {
				configTime(networkConfig.ntpUtcOffset, networkConfig.ntpDaylightUtcOffset, networkConfig.ntpHost.c_str());
			}
		} else {
			heating::logger.printf("NTP disabled\n");
		}

		struct tm timeinfo;
		if (!getLocalTime(&timeinfo)) {
			heating::logger.println("Failed to obtain network time. Setting up from RTC\n");
			heating::setLocalTimeFromRTC(networkConfig.timeZone, networkConfig.ntpUtcOffset, networkConfig.ntpDaylightUtcOffset);
		} else if (networkConfig.ntpEnabled) {
			heating::logger.println(&timeinfo);
			heating::logger.println("\nObtained network time, setting up RTC\n");
			heating::setRTCfromLocalTime();
		}

		if (!MDNS.begin(WiFi.getHostname())) { //CONFIG_MDNS_TASK_STACK_SIZE 4096
			heating::logger.printf("Error starting mDNS\n");
		} else {
			MDNS.addService(networkConfig.hostname.c_str(), "http", networkConfig.listenPort);
		}
	}

}

void WifiSetUp(config::WiFiConfig const &wifiConfig, config::NetworkConfig const &networkConfig, config::APConfig const &apConfig, bool startAP) {
	if (startAP) {
		heating::logger.printf("Starting Access Point\n");
		if (heating::controller) {
			heating::controller->stopBluetoothScan();
			heating::controller->waitUntilBluetoothScanFinishAndDeinitBLE();
			heating::logger.printf("Starting Access Point. Stopped bluetooth scan\n");
		}

		WiFi.mode(WIFI_AP);
		WiFi.setSleep(WIFI_PS_NONE);

		bool softAPResult = WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel);

		heating::logger.printf("Soft AP start: %d\n", softAPResult);

		WiFi.onEvent(
			[apConfig](arduino_event_id_t event, arduino_event_info_t info) {
				IPAddress ip, gateway, subnetMask;
				ip.fromString(apConfig.ip.c_str());
				gateway.fromString(apConfig.gateway.c_str());
				subnetMask.fromString(apConfig.subnetMask.c_str());

				bool configResult = WiFi.softAPConfig(ip, gateway, subnetMask);
				WiFi.softAPsetHostname(apConfig.hostname.c_str());

				heating::logger.printf("Started Access Point. Hostname: '%s'. IP address: %s. config: %d\n", WiFi.softAPgetHostname(), WiFi.softAPIP().toString().c_str(), configResult);

				heating::setLocalTimeFromRTC(apConfig.timeZone, apConfig.ntpUtcOffset, apConfig.ntpDaylightUtcOffset);

				if (heating::rest) {
					heating::logger.printf("Restarting REST service\n");
					heating::rest->restart();
				}
				heating::wifiAPMode = true;

				if (!MDNS.begin("heating")) {
					heating::logger.printf("Error starting mDNS\n");
				} else {
					MDNS.addService(apConfig.hostname.c_str(), "http", apConfig.listenPort);
				}
			}, arduino_event_id_t::ARDUINO_EVENT_WIFI_AP_START);



		WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) { heating::logger.printf("AccessPoint client IP assigned: '%s'\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str()); }, arduino_event_id_t::ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);

		WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) { heating::logger.printf("AccessPoint client connected MAC: " MACSTR "\n",MAC2STR(info.wifi_ap_staconnected.mac)); }, arduino_event_id_t::ARDUINO_EVENT_WIFI_AP_STACONNECTED);

	} else {
		heating::logger.printf("networkConfig\n"
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

	heating::logger.initialize(networkConfig.loggerEnabled, networkConfig.loggerHost, networkConfig.loggerPort);
}

void setup() {
	delay(2500); // wait for monitor

	heating::logger.printf("Free memory %d/%d (minimum was: %d) MaxAlloc: %d STARTUP\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	pinMode(ONBOARD_LED, OUTPUT);
	digitalWrite(ONBOARD_LED, HIGH);
	digitalWrite(ONBOARD_LED, LOW);

	delay(500);
	Serial.begin(115200);

	heating::logger.println("Starting up");

#ifdef CONFIG_BT_CLASSIC_ENABLED
	heating::logger.printf("CLASSIC BT ENABLED\n");
#else
	heating::logger.printf("BLE BT ENABLED\n");
#endif


#if SOC_UART_NUM > 1
	heating::logger.printf("SERIAL 1 ENABLED\n");
#endif

	if (!SPIFFS.begin(false)) {
		heating::logger.println("An Error has occurred while mounting SPIFFS");
	}

	config::readDebugOptions();

	heating::logger.printf("Free memory %d/%d (minimum was: %d) MaxAlloc: %d SPIFFS\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	auto networkConfig = config::getNetworkConfig();

	if (networkConfig.rtcEnabled) {
		auto rtcpins = config::getRTCPins();
		heating::logger.printf("Configuring i2c RTC on sda %d scl %d\n", rtcpins.sda, rtcpins.scl);
		Wire.begin(rtcpins.sda, rtcpins.scl);
		heating::startRTC(networkConfig.rtcEnabled);
	} else {
		heating::logger.printf("RTC battery clock disabled\n");
	}

	heating::logger.printf("Free memory %d/%d (minimum was: %d) MaxAlloc: %d RTC\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	auto wifiConfig = config::getWiFiConfig();
	auto apConfig = config::getAPConfig();

	bool startAP = wifiConfig.ssid.empty() /*|| TODO PUSHBUTTON PRESSED */;

	heating::controller = new heating:: HeatingController();
	heating::rest = std::make_unique<heating::REST>(*heating::controller, startAP ? apConfig.listenPort : networkConfig.listenPort);

	heating::logger.printf("Free memory %d/%d (minimum was: %d) MaxAlloc: %d STUFF\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

	WifiSetUp(wifiConfig, networkConfig, apConfig, startAP);
	heating::logger.printf("Free memory %d/%d (minimum was: %d) MaxAlloc: %d WIFI\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
}

void loop() {
	static unsigned long lastMillis = 0;

	auto now = millis();
	if (heating::millisDurationPassed(now, lastMillis, 10000)) {
		lastMillis = now;

		heating::controller->operate();
		heating::logger.printf("Free memory %d/%d (minimum was: %d) MaxAlloc: %d MinPeekStack: %d boxTemp: %f, UpTime: %lds\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), uxTaskGetStackHighWaterMark(nullptr), heating::rtcGetTemp(), millis()/1000);

		if (!WiFi.isConnected()) {
			heating::logger.printf("WiFi not connected. Reconnecting.\n");
			WiFi.reconnect();
		} else {
			heating::logger.printf("WiFi IP Address: %s\n", WiFi.localIP().toString().c_str());
		}
	}

	heating::controller->loop();

	if (heating::wifiAPMode || WiFi.isConnected()) {
		heating::rest->handle();
	}
}
