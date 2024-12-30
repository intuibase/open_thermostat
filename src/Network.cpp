

#include <sstream>
#include <WiFi.h>

#include "Network.h"
#include "config.h"

namespace heating {

static const char *getEncryptionStr(wifi_auth_mode_t mode) {
	switch (mode) {
		case WIFI_AUTH_OPEN:
			return "open";
		case WIFI_AUTH_WEP:
			return "WEP";
		case WIFI_AUTH_WPA_PSK:
			return "WPA_PSK";
		case WIFI_AUTH_WPA2_PSK:
			return "WPA2_PSK";
		case WIFI_AUTH_WPA_WPA2_PSK:
			return "WPA_WPA2_PSK";
		case WIFI_AUTH_WPA2_ENTERPRISE:
			return "WPA2_ENTERPRISE";
		default:
			return "open";
	}
}

void getWiFiNetworks(std::ostream &ss) {
	auto noNetworks = WiFi.scanNetworks();
	ss << "[";
	for (auto n = 0; n < noNetworks; ++n) {
		ss << "{\"ssid\": \"" << WiFi.SSID(n).c_str() << "\", \"rssi\": " << WiFi.RSSI(n) << ", \"channel\": " << WiFi.channel(n) << ", \"encryption\": \"" << getEncryptionStr(WiFi.encryptionType(n)) << "\"}";
		if (n + 1 < noNetworks) {
			ss << ", ";
		}
	}
	ss << "]";
}

void getWiFiSSID(std::ostream &ss) {
	auto wifiConfig = config::getWiFiConfig();
	ss << "{ \"ssid\": \"" << wifiConfig.ssid << "\"}";
}

}
