#pragma once


#include "config.h"
#include "Logger.h"
#include "TimeHelpers.h"

#include <PubSubClient.h>
#include <ESPPubSubClientWrapper.h>
#include <ViewableStringBuf.h>
#include <ostream>
#include <string_view>

namespace heating {

using namespace std::string_view_literals;

class MyPubSub : public ESPPubSubClientWrapper {
public:
	MyPubSub(const char *domain, uint16_t port = 1883) : ESPPubSubClientWrapper(domain, port) {
	}

	boolean publish(std::string_view topic, std::string_view payload, boolean retained) {
		if (!connected()) {
			return false;
		}

		try {
			uint32_t sizeOfPacket = 2 + topic.length() + payload.length(); // 2 bytes for topic length
			auto variableHeaderLen = howManyBytesWeNeedToEncodeSize(sizeOfPacket);
			uint8_t header[3 + variableHeaderLen];  // 1 byte for header, 2 bytes topic len

			header[0] = MQTTPUBLISH;
			if (retained) {
				header[0] |= 0x01;
			}

			// if (qos == 0) {
			// 	header[0] |= 0x00;
			// } else if (qos == 1) {
			// 	header[0] |= 0x02;
			// } else if (qos == 2) {
			// 	header[0] |= 0x04;
			// }

			header[variableHeaderLen + 1] = (topic.length() >> 8);
			header[variableHeaderLen + 2] = (topic.length() & 0xFF);

			uint8_t pos = 1;
			uint16_t len = sizeOfPacket;
			do {
				uint8_t digit = len  & 127; //digit = len %128
				len >>= 7; //len = len / 128
				if (len > 0) {
					digit |= 0x80;
				}
				header[pos++] = digit;
			} while(len>0);

			_wiFiClient.write(header, 3 + variableHeaderLen);
			_wiFiClient.write(reinterpret_cast<const uint8_t *>(topic.data()), topic.length());
			_wiFiClient.write(reinterpret_cast<const uint8_t *>(payload.data()), payload.length());

			return true;
		} catch (std::out_of_range const &e) {
			DBGLOGMQTT("Unable to publish: %s\n", e.what());
			return false;
		}
		//     HXXXX00topicpayload  -- 00 topic len H header XXXX variable header len (contains 00 + topic + payload lenghts)
	}

protected:
	uint8_t howManyBytesWeNeedToEncodeSize(uint32_t length) {
		if (length < 128) { return 1; }
		if (length < 16384) { return 2; }
		if (length < 2097152) { return 3; }
		if (length < 268435456) { return 4; }
		throw std::out_of_range("payload too long");
	}
};

class MQTT {
public:
	using getRoomStatus_t = std::function<void(std::ostream &)>;
	using getRoomCount_t = std::function<std::size_t()>;
	using getEmsMetrics_t = std::function<void(std::ostream &)>;

	MQTT(getRoomCount_t getRoomsCount, getRoomStatus_t getRoomsStatus, getEmsMetrics_t getEmsMetrics) : config_(config::getMqttConfig()), client_{config_.brokerAddress.c_str(), config_.brokerPort}, getRoomsStatus_{std::move(getRoomsStatus)}, getEmsMetrics_(std::move(getEmsMetrics)) {
		DBGLOGMQTT("Enabled: %d\n", config_.enabled);
		DBGLOGMQTT("%s:%d\n", config_.brokerAddress.c_str(), config_.brokerPort );
		DBGLOGMQTT("publish interval %d, keep alive inteval: %d\n", config_.interval, config_.keepAlive);
		DBGLOGMQTT("clientId '%s', base: '%s'\n", config_.clientId.c_str(), config_.base.c_str());

		if (!config_.enabled) {
			return;
		}

		client_.on("homeassistant/status", [this](char* topic, uint8_t* payload, unsigned int payloadLen) {
			DBGLOGMQTT("HomeAssistant '%s' payload: '%s'\n", topic, payload);
		});

		client_.onConnect([this, getRoomsCount](uint16_t connCount) {
			DBGLOGMQTT("Connected to broker %d\n", connCount);
			publishHADiscovery(getRoomsCount());
		});

		client_.connect(config_.clientId.c_str(),
			config_.username.empty() ? nullptr : config_.username.c_str(),
			config_.password.empty() ? nullptr : config_.password.c_str(),
			"open_thermostat/status",
			0,
			false,
			"off",
			true
			);
	}

	void operate() {
		if (!config_.enabled) {
			return;
		}

		if (client_.connected()) {
			publishRoomData();
			publishStatus();
			publishDeviceStatus();
			publishEmsMetrics();
		}
	}

	void loop() {
		if (!config_.enabled) {
			return;
		}
		client_.loop();
	}

private:
	void publishHADiscovery(size_t roomCount) {
		DBGLOGMQTT("publishHADiscovery\n");

		auto payload = "{\
			\"name\": \"OpenThermostat\",\
			\"uniq_id\": \"open_thermostat\",\
			\"object_id\": \"opth_status\",\
			\"state_topic\": \"open_thermostat/status\",\
			\"device_class\": \"power\",\
			\"payload_on\": \"on\",\
			\"payload_off\": \"off\",\
			\"dev\": {\
				\"name\": \"OpenThermostat\",\
				\"sw\": \"1.0.0\",\
				\"mf\": \"intuibase\",\
				\"mdl\": \"OpenThermostat\",\
				\"ids\": [\
					\"open_thermostat\"\
				]\
			}\
		}"sv;
		client_.publish("homeassistant/binary_sensor/open_thermostat/status/config"sv, payload.data(), true);

		for (auto room = 0; room < roomCount; ++room) {
			publishRoomBinarySensor(room, "enabled"sv, "Heating enabled"sv);
			publishRoomSensor(room, "curr_temp"sv, "Current temperature"sv, "currentTemp"sv, "/ 100"sv, "°C"sv, "measurement"sv, "temperature"sv);
			publishRoomSensor(room, "temp_set"sv, "Temperature set"sv, "tempSet"sv, "/ 100 "sv, "°C"sv, "measurement"sv, "temperature"sv);
			publishRoomSensor(room, "name"sv, "Room name"sv, "name"sv, ""sv, {}, {});
			publishRoomSensor(room, "battery"sv, "Battery level"sv, "batteryLevel"sv, ""sv, "%"sv, "measurement"sv, "battery"sv);
			publishRoomSensor(room, "humidity"sv, "Humidity"sv, "currentHumidity"sv, "/ 100"sv, "%"sv, "measurement"sv, "humidity"sv);
		}

		publishSensor("device_status"sv, "opth_memory_free"sv, "OpenThermostat free memory"sv, "mem_free"sv, ""sv, "b", "measurement"sv, "data_size"sv);
		publishSensor("device_status"sv, "opth_memory_min_free"sv, "OpenThermostat minimum free memory"sv, "mem_min_free"sv, ""sv, "b", "measurement"sv, "data_size"sv);
		publishSensor("device_status"sv, "opth_memory_max_alloc"sv, "OpenThermostat max allocable memory block"sv, "mem_max_alloc"sv, ""sv, "b", "measurement"sv, "data_size"sv);
		publishSensor("device_status"sv, "opth_temperature"sv, "OpenThermostat RTC temperature inside box"sv, "temperature"sv, ""sv, "°C"sv, "measurement"sv, "temperature"sv);
		publishSensor("device_status"sv, "opth_uptime"sv, "OpenThermostat device uptime"sv, "uptime"sv, ""sv, "s"sv, "total_increasing"sv, "duration"sv);

		publishSensor("ems_metrics"sv, "opth_energy"sv, "Total energy consumption"sv, "totalEnergyUsedKwh"sv, ""sv, "kWh"sv, "measurement"sv, "energy"sv);
		publishSensor("ems_metrics"sv, "opth_energy_warm_water"sv, "Energy used for warm water heating"sv, "warmWaterEnergyUsedKwh"sv, ""sv, "kWh"sv, "measurement"sv, "energy"sv);
		publishSensor("ems_metrics"sv, "opth_energy_heating"sv, "Energy used for space heating"sv, "heatingEnergyUsedKwh"sv, ""sv, "kWh"sv, "measurement"sv, "energy"sv);
	}


	void publishDeviceStatus() {
		auto now = millis();
		if (lastPublishDeviceStatus_ != 0 && !millisDurationPassed(lastPublishDeviceStatus_, 1000ul * config_.interval)) {
			decltype(now) timeToWait = static_cast<decltype(lastPublishDeviceStatus_)>(config_.interval) * 1000ul;
			timeToWait = timeToWait - (now - lastPublishDeviceStatus_);
			DBGLOGMQTT("publishDeviceStatus: waiting for publish interval (%ds) last: %ld, now: %ld, still need to wait for: %ld ms \n", config_.interval, lastPublishDeviceStatus_, now, timeToWait);
			return;
		}

		lastPublishDeviceStatus_ = now;

		ViewableStringBuf payloadBuf;
		std::ostream ss(&payloadBuf);

		ss << "{\"mem_free\":" << ESP.getFreeHeap() << ",";
		ss << "\"mem_min_free\":" << ESP.getMinFreeHeap() << ",";
		ss << "\"mem_max_alloc\":" << ESP.getMaxAllocHeap() << ",";
		ss << "\"temperature\":" << heating::rtcGetTemp() << ",";
		ss << "\"uptime\":" << millis()/1000 ;
		ss << "}";

		DBGLOGMQTT("publishDeviceStatus %zu\n", payloadBuf.view().length());

		client_.publish("open_thermostat/device_status"sv, payloadBuf.view(), false);
	}

	void publishEmsMetrics() {
		auto now = millis();
		if (lastPublishEmsMetrics_ != 0 && !millisDurationPassed(lastPublishEmsMetrics_, 1000ul * config_.interval)) {
			decltype(now) timeToWait = static_cast<decltype(lastPublishEmsMetrics_)>(config_.interval) * 1000ul;
			timeToWait = timeToWait - (now - lastPublishEmsMetrics_);
			DBGLOGMQTT("publishEmsMetrics: waiting for publish interval (%ds) last: %ld, now: %ld, still need to wait for: %ld ms \n", config_.interval, lastPublishEmsMetrics_, now, timeToWait);
			return;
		}

		lastPublishEmsMetrics_ = now;

		ViewableStringBuf payloadBuf;
		std::ostream ss(&payloadBuf);

		getEmsMetrics_(ss);

		DBGLOGMQTT("publishEmsMetrics %zu\n", payloadBuf.view().length());

		client_.publish("open_thermostat/ems_metrics"sv, payloadBuf.view(), false);
	}

	void publishStatus() {
		if (lastPublishStatus_ != 0 && !millisDurationPassed(lastPublishStatus_, 1000ul * config_.keepAlive)) {
			auto now = millis();
			unsigned long timeToWait = static_cast<decltype(lastPublishStatus_)>(config_.keepAlive) * 1000ul;
			timeToWait = timeToWait - (now - lastPublishStatus_);
			DBGLOGMQTT("publishStatus: waiting for keepalive interval (%ds) last: %ld, now: %ld, still need to wait for: %ld ms \n", config_.keepAlive, lastPublishStatus_, now, timeToWait);
			return;
		}

		lastPublishStatus_ = millis();
		DBGLOGMQTT("publishStatus\n");
		client_.publish("open_thermostat/status"sv, "on"sv, true);
	}

	void publishRoomData() {
		auto now = millis();
		if (lastPublishRoomData_ != 0 && !millisDurationPassed(lastPublishRoomData_, 1000ul * config_.interval)) {
			decltype(now) timeToWait = static_cast<decltype(lastPublishRoomData_)>(config_.interval) * 1000ul;
			timeToWait = timeToWait - (now - lastPublishRoomData_);
			DBGLOGMQTT("publishRoomData: waiting for publish interval (%ds) last: %ld, now: %ld, still need to wait for: %ld ms \n", config_.interval, lastPublishRoomData_, now, timeToWait);
			return;
		}

		lastPublishRoomData_ = now;

		ViewableStringBuf payloadBuf;
		std::ostream ss(&payloadBuf);

		ss << "{\"rooms\": ";
		getRoomsStatus_(ss);
		ss << "}";

		DBGLOGMQTT("publishRoomData %zu\n", payloadBuf.view().length());

		client_.publish("open_thermostat/room_data"sv, payloadBuf.view(), false);
	}

	void publishRoomBinarySensor(uint16_t roomNo, std::string_view sensorName, std::string_view sensorFriendlyName) {
		ViewableStringBuf payloadBuf;
		std::ostream ss(&payloadBuf);
		ss << "{";
		ss << "\"name\": \"" << sensorFriendlyName << "\",";
		ss << "\"uniq_id\": \"opth_room_" << roomNo << "_" << sensorName << "\",";
		ss << "\"obj_id\": \"opth_room_" << roomNo << "_" << sensorName << "\",";
		ss << "\"stat_t\": \"open_thermostat/room_data\",";
		ss << "\"pl_on\": true, \"pl_off\": false,";
		ss << "\"val_tpl\": \"{{value_json.rooms[" << roomNo << "].enabled if value_json.rooms[" << roomNo << "].enabled is defined else 'false'}}\",";
 		ss << "\"dev\": {";
		ss << "\"ids\": [ \"open_thermostat_room_" << roomNo << "\" ],";
		ss << "\"name\": \"OpenThermostat Room " << roomNo + 1 << "\",";
		ss << "\"mf\": \"intuibase\",";
		ss << "\"mdl\": \"OpenThermostat\",";
		ss << "\"via_device\": \"open_thermostat\"";
		ss << "},";
		ss << "\"avty\": [";
		ss << "{ \"t\": \"open_thermostat/room_data\", \"val_tpl\": \"{{ \\\"online\\\" if value_json.rooms[" << roomNo << "].enabled is defined else \\\"offline\\\" }}\" },";
		ss << "{ \"t\": \"open_thermostat/status\", \"val_tpl\": \"{{ \\\"online\\\" if value == \\\"on\\\" else \\\"offline\\\" }}\" }";
		ss << "], \"avty_mode\": \"all\"";
		ss << "}";

		ViewableStringBuf topicBuf;
		std::ostream topic(&topicBuf);
		topic << "homeassistant/binary_sensor/open_thermostat/opth_room_"<< roomNo <<"_" << sensorName << "/config";

		client_.publish(topicBuf.view(), payloadBuf.view(), true);
	}


	void publishRoomSensor(uint16_t roomNo, std::string_view sensorName, std::string_view sensorFriendlyName, std::string_view jsonValueName, std::string_view valueOperation, std::string_view unit, std::string_view stateClass, std::string_view devClass = {}) {
		ViewableStringBuf payloadBuf;
		std::ostream ss(&payloadBuf);
		ss << "{";
		ss << "\"name\": \"" << sensorFriendlyName << "\",";
		ss << "\"uniq_id\": \"opth_room_" << roomNo << "_" << sensorName << "\",";
		ss << "\"obj_id\": \"opth_room_" << roomNo << "_" << sensorName << "\",";
		ss << "\"stat_t\": \"open_thermostat/room_data\",";
		if (!unit.empty()) {
			ss << "\"unit_of_meas\": \"" << unit << "\",";
		}
		if (!stateClass.empty()) {
			ss << "\"stat_cla\": \"" << stateClass << "\",";
		}
		if (!devClass.empty()) {
			ss << "\"dev_cla\": \"" << devClass << "\",";
		}
		ss << "\"val_tpl\": \"{{(value_json.rooms[" << roomNo << "]." << jsonValueName << valueOperation << ") if value_json.rooms[" << roomNo << "]." << jsonValueName << " is defined else '0'}}\",";
		ss << "\"dev\": { \"ids\": [ \"open_thermostat_room_" << roomNo << "\" ] },"; // dev
		ss << "\"avty\": [";
		ss << "{ \"t\": \"open_thermostat/room_data\", \"val_tpl\": \"{{ \\\"online\\\" if value_json.rooms[" << roomNo << "]." << jsonValueName << " is defined else \\\"offline\\\" }}\" },";
		ss << "{ \"t\": \"open_thermostat/status\", \"val_tpl\": \"{{ \\\"online\\\" if value == \\\"on\\\" else \\\"offline\\\" }}\" }";
		ss << "], \"avty_mode\": \"all\"";
		ss << "}";

		ViewableStringBuf topicBuf;
		std::ostream topic(&topicBuf);
		topic << "homeassistant/sensor/open_thermostat/opth_room_"<< roomNo <<"_" << sensorName << "/config";

		client_.publish(topicBuf.view(), payloadBuf.view(), true);
	}

	void publishSensor(std::string_view stateTopic, std::string_view sensorUniqueId, std::string_view sensorFriendlyName, std::string_view jsonValueName, std::string_view valueOperation, std::string_view unit, std::string_view stateClass, std::string_view devClass = {}) {
		ViewableStringBuf payloadBuf;
		std::ostream ss(&payloadBuf);
		ss << "{";
		ss << "\"name\": \"" << sensorFriendlyName << "\",";
		ss << "\"uniq_id\": \"" << sensorUniqueId  << "\",";
		ss << "\"obj_id\": \"" << sensorUniqueId  << "\",";
		ss << "\"stat_t\": \"open_thermostat/" << stateTopic << "\",";
		if (!unit.empty()) {
			ss << "\"unit_of_meas\": \"" << unit << "\",";
		}
		if (!stateClass.empty()) {
			ss << "\"stat_cla\": \"" << stateClass << "\",";
		}
		if (!devClass.empty()) {
			ss << "\"dev_cla\": \"" << devClass << "\",";
		}
		ss << "\"val_tpl\": \"{{(value_json." << jsonValueName << valueOperation << ") if value_json." << jsonValueName << " is defined else '0'}}\",";
		ss << "\"dev\": { \"ids\": [ \"open_thermostat\" ] },"; // dev
		ss << "\"avty\": [";
		ss << "{ \"t\": \"open_thermostat/" << stateTopic << "\", \"val_tpl\": \"{{ \\\"online\\\" if value_json." << jsonValueName << " is defined else \\\"offline\\\" }}\" },";
		ss << "{ \"t\": \"open_thermostat/status\", \"val_tpl\": \"{{ \\\"online\\\" if value == \\\"on\\\" else \\\"offline\\\" }}\" }";
		ss << "], \"avty_mode\": \"all\"";
		ss << "}";

		ViewableStringBuf topicBuf;
		std::ostream topic(&topicBuf);
		topic << "homeassistant/sensor/open_thermostat/" << sensorUniqueId << "/config";

		client_.publish(topicBuf.view(), payloadBuf.view(), true);
	}

private:
	config::MqttConfig config_;
	MyPubSub client_;
	unsigned long lastPublishRoomData_ = 0;
	unsigned long lastPublishDeviceStatus_ = 0;
	unsigned long lastPublishStatus_ = 0;
	unsigned long lastPublishEmsMetrics_ = 0;

	getRoomStatus_t getRoomsStatus_;
	getEmsMetrics_t getEmsMetrics_;
};
}
