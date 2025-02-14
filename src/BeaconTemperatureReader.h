#pragma once

#include "config.h"
#include "Logger.h"
#include "BeaconBleAddress.h"
#include "TimeHelpers.h"

#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <esp_gap_ble_api.h>

#include <atomic>
#include <memory>
#include <set>
#include <string_view>
#include <vector>


namespace heating {

class BeaconTemperatureReader : public NimBLEScanCallbacks {
private:
	struct ServiceDataView {
		uint8_t *data = nullptr;
		uint8_t length = 0;
	};

public:
	using BleDevices_t = std::set<BleAddress_t>;

	using ReportTemperature_t = std::function<void(BleAddress_t, std::optional<int16_t> temperature, std::optional<int16_t> humidity, std::optional<int8_t> battery)>;

	BeaconTemperatureReader(ReportTemperature_t pushTemperature) : pushTemperature_(pushTemperature) {
		stReader_ = this;

		esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

		if (NimBLEDevice::init("HeatingController")) {
			DBGLOGTR("NimBLEDevice initialized successfully.\n");
		}
		NimBLEScan *pBLEScan = NimBLEDevice::getScan();

		pBLEScan->setScanCallbacks(this, true);
		pBLEScan->setActiveScan(true);
		pBLEScan->setInterval(100); // 100ms
		pBLEScan->setWindow(80);    // 80ms

		DBGLOGTR("BLE initialized successfully.\n");
	}

	void shutDownBLE() { NimBLEDevice::deinit(true); }

	bool isScanPending() { return bluetoothScanPending_; }

	void triggerScan() {
		if (bluetoothScanPending_) {
			DBGLOGTR("Scan pending\n");
			return;
		}

		auto now = millis();
		if (lastScanFinishedMillis_ != 0 && !millisDurationPassed(now, lastScanFinishedMillis_, 1000ul * config_.scanInterval)) {
			DBGLOGTR("Scan interval %ds didn't pass yet\n", config_.scanInterval);
			return;
		}

		bluetoothScanPending_ = true;
		DBGLOGTR("Scan start %ds\n", config_.scanTime);

		NimBLEScan *pBLEScan = NimBLEDevice::getScan();
		auto res = pBLEScan->start(config_.scanTime * 1000, false, true);

		DBGLOGTR("Scan start  result: %d\n", res);
	}

	void scanFinished() {
		bluetoothScanPending_ = false;
		lastScanFinishedMillis_ = millis();
		NimBLEDevice::getScan()->clearResults();
		DBGLOGTR("Scan finished\n");
	}

	void onResult(NimBLEAdvertisedDevice const *advertisedDevice) final {
		auto payload = advertisedDevice->getPayload();
		auto addr = advertisedDevice->getAddress();

		// DBGLOGTR("ON RESULT SERVICE DATA COUNT %zu / %s, payload size: %zu address: %s\n", advertisedDevice->getServiceDataCount(), advertisedDevice->getName().c_str(), payload.size(), advertisedDevice->getAddress().toString().c_str());

		parseAdvertisment(addr.getBase()->val, advertisedDevice->getRSSI(), payload.data(), payload.size());
	}

	void onDiscovered(const NimBLEAdvertisedDevice *advertisedDevice) final {
		// DBGLOGTR("ON DISCOVERED  SERVICE DATA COUNT %zu / %s, payload size: %zu\n", advertisedDevice->getServiceDataCount(), advertisedDevice->getName().c_str(), payload.size());
	}
	void onScanEnd(const NimBLEScanResults &scanResults, int reason) {
		DBGLOGTR("onScanEnd %d / %d\n", scanResults.getCount(), reason);
		scanFinished();
	}

	void parseAdvertisment(uint8_t const *bda, int rssi, uint8_t *payload, size_t payloadSize) {
		if (payloadSize < 2) {
			return;
		}

		std::string_view deviceName;
		std::vector<ServiceDataView> serviceData;
		uint8_t *frame = payload;

		while (frame < payload + payloadSize) {
			uint8_t frameLen = frame[0];
			if (frameLen == 0) {
				break;
			}
			uint8_t frameType = frame[1];
			frameLen--; // remove type byte from length
			frame += 2; // skip type and length bytes
			// DBGLOGTR("Frame type: %d 0x%02X %d\n", frameType, frameType, frameLen);
			switch (frameType) {
				case ESP_BLE_AD_TYPE_NAME_SHORT:
				case ESP_BLE_AD_TYPE_NAME_CMPL:
					// DBGLOGTR("Device name: %s\n", std::string((char*)frame, frameLen).c_str());
					deviceName = {reinterpret_cast<char *>(frame), frameLen};
					if (deviceName != "MJ_HT_V1" && deviceName.substr(0, 4) != "ATC_") {
						return; // don't parse service data
					}
					break;
				case ESP_BLE_AD_TYPE_SERVICE_DATA:
					if (frameLen < 2) {
						return;
					}
					// uint16_t uuid = *(uint16_t*)frame;
					serviceData.emplace_back(ServiceDataView{frame + 2, static_cast<uint8_t>(frameLen - 2)});
				break;
			}
			frame += frameLen;
		}

		if (deviceName == "MJ_HT_V1") {
			parseMJ_HT_V1(BleAddress_t{bda[5], bda[4], bda[3], bda[2], bda[1], bda[0]}, rssi, serviceData);
		} else if (deviceName.substr(0, 4) == "ATC_") {
			parseATC(BleAddress_t{bda[5], bda[4], bda[3], bda[2], bda[1], bda[0]}, rssi, serviceData);
		}
	}

	void parseMJ_HT_V1(BleAddress_t bda, int rssi, std::vector<ServiceDataView> const &serviceData) {
		if (serviceData.size() == 0) {
			DBGLOGTR("No service data for " PRiBleAddress "\n", PRaBleAddress(bda));
			return;
		}

		DBGLOGTR("%s: " PRiBleAddress ", serviceDataCount: %d RSSI: %d\n", "MJ_HT_V1", PRaBleAddress(bda), serviceData.size(),  rssi);

		for (int service = 0; service < 1; ++service) { // first is always valid
			if (serviceData[service].length < 15) {
				// DBGLOGTR("Service data[%d] too short\n", service);
				// for (int i = 0; i < serviceData[service].length; ++i) {
				// 	logger.printf("%02X ", serviceData[service].data[i]);
				// }
				// logger.printf("\n");
				continue;
			}
			switch (serviceData[service].data[11]) {
				case 0x04: {
					int16_t temperature = (serviceData[service].data[15] * 256 + serviceData[service].data[14]) * 10;
					pushTemperature_(bda, temperature, {}, {});
					DBGLOGTR("TEMPERATURE_EVENT ONLY: %d\n", temperature);
					break;
				}
				case 0x06: {
					int16_t humidity = (serviceData[service].data[15] * 256 + serviceData[service].data[14]) * 10;
					DBGLOGTR("HUMIDITY_EVENT: %d \n", humidity);
					pushTemperature_(bda, {}, humidity, {});
					break;
				}
				case 0x0A: {
					DBGLOGTR("BATTERY_EVENT: %d%%\n", serviceData[service].data[14]);
					pushTemperature_(bda, {}, {}, serviceData[service].data[14]);
					break;
				}
				case 0x0D: {
					int16_t temperature = (serviceData[service].data[15] * 256 + serviceData[service].data[14]) * 10;
					int16_t humidity = (serviceData[service].data[17] * 256 + serviceData[service].data[16]) * 10;
					DBGLOGTR("TEMPERATURE_HUMIDITY EVENT: %d %d\n", temperature, humidity);
					pushTemperature_(bda, temperature, humidity, {});
				}
				break;
			}
		}
	}

	void parseATC(BleAddress_t bda, int rssi, std::vector<ServiceDataView> const &serviceData) {
		if (serviceData.size() == 0) {
			DBGLOGTR("No service data for " PRiBleAddress "\n", PRaBleAddress(bda));
			return;
		}

		DBGLOGTR("%s: " PRiBleAddress ", serviceDataCount: %d RSSI: %d\n", "ATC", PRaBleAddress(bda), serviceData.size(),  rssi);
		for (int service = 0; service < 1 /*serviceDataCount*/; ++service) {
			if (serviceData[service].length < 12) {
				// DBGLOGTR("Service data[%d] too short\n", service);
				// for (int i = 0; i < serviceData[service].length; ++i) {
				// 	logger.printf("%02X ", serviceData[service].data[i]);
				// }
				// logger.printf("\n");
				continue;
			}
			int16_t temperature = (serviceData[service].data[6] * 256 + serviceData[service].data[7]) * 10;
			int16_t humidity = serviceData[service].data[8] * 100;
			int8_t battery = serviceData[service].data[9];
			DBGLOGTR("TEMPERATURE_HUMIDITY_BATTERY EVENT: %d %d %d\n", temperature, humidity, battery);
			pushTemperature_(bda, temperature, humidity, battery);
		}
	}


private:
	static BeaconTemperatureReader *stReader_;
	ReportTemperature_t pushTemperature_;
	std::atomic_bool bluetoothScanPending_ = false;
	config::BluetoothConfig config_ = config::getBluetoothConfig();
	unsigned long lastScanFinishedMillis_ = 0;
};
}

heating::BeaconTemperatureReader *heating::BeaconTemperatureReader::stReader_ = nullptr;
