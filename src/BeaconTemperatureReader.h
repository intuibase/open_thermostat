#pragma once

#include"config.h"
#include "Logger.h"
#include "BeaconBleAddress.h"
#include "TimeHelpers.h"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_bt_device.h>
#include <esp32-hal-bt.h>
#include <atomic>
#include <memory>
#include <set>
#include <string_view>
#include <vector>


namespace heating {

class BeaconTemperatureReader {
private:
	struct ServiceDataView {
		uint8_t *data = nullptr;
		uint8_t length = 0;
	};

	 esp_ble_scan_params_t ble_scan_params {
		.scan_type              = BLE_SCAN_TYPE_ACTIVE,
		.own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
		.scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
		// .scan_interval          = 0x50,
		// .scan_window            = 0x30,
		.scan_interval          = (uint16_t)(100/0.625),  //100ms
		.scan_window            = (uint16_t)(80/0.625), //80ms
		.scan_duplicate         = BLE_SCAN_DUPLICATE_ENABLE
	} ;

public:
	using BleDevices_t = std::set<BleAddress_t>;

	using ReportTemperature_t = std::function<void(BleAddress_t, std::optional<int16_t> temperature, std::optional<int16_t> humidity, std::optional<int8_t> battery)>;

	BeaconTemperatureReader(ReportTemperature_t pushTemperature) : pushTemperature_(pushTemperature) {
		stReader_ = this; // needed to correlate c-style bt callback with out class

		if (!btStart()) {
			DBGLOGTR("BeaconTemperatureReader bluetooth init failed\n");
			return;
		}

		//  *
		//  * If the app calls esp_bt_controller_enable(ESP_BT_MODE_BLE) to use BLE only then it is safe to call
		//  * esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT) at initialization time to free unused BT Classic memory.
		//  *
		//  * If the mode is ESP_BT_MODE_BTDM, then it may be useful to call API esp_bt_mem_release(ESP_BT_MODE_BTDM) instead,
		//  * which internally calls esp_bt_controller_mem_release(ESP_BT_MODE_BTDM) and additionally releases the BSS and data
		//  * consumed by the BT/BLE host stack to heap. For more details about usage please refer to the documentation of
		//  * esp_bt_mem_release() function

		#ifndef CONFIG_BT_CLASSIC_ENABLED
			esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
		#else
			esp_bt_mem_release(ESP_BT_MODE_BTDM);
		#endif



		if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
			if (auto res = esp_bluedroid_init(); res != ESP_OK) {
				DBGLOGTR("bluedroid init error %s (%d)\n", esp_err_to_name(res), res);
				return;
			}
		}

		if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
			DBGLOGTR("bluedroid enable\n");
			if (auto res = esp_bluedroid_enable(); res != ESP_OK) {
				DBGLOGTR("bluedroid enable error %s (%d)\n", esp_err_to_name(res), res);
				return;
			}
		}

		DBGLOGTR("esp_bluedroid_get_status %d\n", esp_bluedroid_get_status());
		DBGLOGTR("esp_bt_controller_get_status %d\n", esp_bt_controller_get_status());


		if (const uint8_t *addr = esp_bt_dev_get_address(); addr != nullptr) {
			DBGLOGTR("Device address " PRiBleAddress "\n", PRaBleAddress(addr));
		}

		if(auto res = esp_ble_gap_register_callback(BeaconTemperatureReader::esp_gap_cb); res != ESP_OK) {
			DBGLOGTR("esp_ble_gap_register_callback error %s (%d)\n", esp_err_to_name(res), res);
			return;
		}

		if(auto res = esp_ble_gap_set_device_name("HeatingController"); res != ESP_OK) {
			DBGLOGTR("esp_ble_gap_set_device_name error %s (%d)\n", esp_err_to_name(res), res);
		}

		#ifdef CONFIG_BLE_SMP_ENABLE
			esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
			if (esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(esp_ble_io_cap_t)) != ESP_OK) {
				DBGLOGTR("Unable to set BLE security param\n");
			}
		#endif

		vTaskDelay(200 / portTICK_PERIOD_MS); // Delay for 200 msecs as a workaround to an apparent Arduino environment issue.
	}


	void shutDownBLE() {
		esp_bluedroid_disable();
		esp_bluedroid_deinit();
		esp_bt_controller_disable();
		esp_bt_controller_deinit();
		// esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
	}

	bool isScanPending() {
		return bluetoothScanPending_;
	}

	void triggerScan() {
		if (bluetoothScanPending_) {
			DBGLOGTR("scan pending\n");
			return;
		}

		auto now = millis();
		if (lastScanFinishedMillis_ != 0 && !millisDurationPassed(now, lastScanFinishedMillis_, 1000ul * config_.scanInterval)) {
			DBGLOGTR("scan interval %ds didn't passed yet\n", config_.scanInterval);
			return;
		}

		bluetoothScanPending_ = true;
		DBGLOGTR("scan start %ds\n", config_.scanTime);


		if(auto res = esp_ble_gap_set_scan_params(&ble_scan_params); res != ESP_OK) {
			DBGLOGTR("esp_ble_gap_set_scan_params error %s (%d)\n", esp_err_to_name(res), res);
			return;
		}

		if(auto res = esp_ble_gap_start_scanning(config_.scanTime); res != ESP_OK) {
			DBGLOGTR("esp_ble_gap_start_scanning(60) error %s (%d)\n", esp_err_to_name(res), res);
			return;
		}

		vTaskDelay(20 / portTICK_PERIOD_MS); // Delay for 200 msecs as a workaround to an apparent Arduino environment issue.
	}

	void scanFinished() {
		bluetoothScanPending_ = false;
		lastScanFinishedMillis_ = millis();
		esp_ble_gap_stop_scanning();
		DBGLOGTR("scan finished\n");
	}

	static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
		switch (event) {
			case ESP_GAP_BLE_SCAN_RESULT_EVT:
				switch(param->scan_rst.search_evt) {
					case ESP_GAP_SEARCH_INQ_CMPL_EVT:
						stReader_->scanFinished();
					break;
					case ESP_GAP_SEARCH_INQ_RES_EVT:
						// DBGLOGTR("ESP_GAP_SEARCH_INQ_RES_EVT rssi: %d result number: %d\n", param->scan_rst.rssi, param->scan_rst.num_resps);
						stReader_->parseAdvertisment(param->scan_rst.bda, param->scan_rst.rssi, param->scan_rst.ble_adv, param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len);
					break;
					default:
						// DBGLOGTR("ESP_GAP_BLE_SCAN_RESULT_EVT type: %d\n", param->scan_rst.search_evt);
					break;
				}
				break;
		}
	}

	void parseAdvertisment(uint8_t const *bda, int rssi, uint8_t *payload, size_t payloadSize) {
		if (payloadSize < 2) {
			return;
		}
		// DBGLOGTR("Payload size: %d\n", payloadSize);

		std::string_view deviceName;
		std::vector<ServiceDataView> serviceData;

		uint8_t *frame = payload;

		while (frame < payload + payloadSize) {
			uint8_t frameLen = frame[0];
			if (frameLen == 0)  {
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
			parseMJ_HT_V1(BleAddress_t{bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]}, rssi, serviceData);
		} else if (deviceName.substr(0, 4) == "ATC_") {
			parseATC(BleAddress_t{bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]}, rssi, serviceData);
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
