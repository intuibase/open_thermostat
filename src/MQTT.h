#pragma once

#include "config.h"
#include "Logging.h"
#include "RTCTimeHelpers.h"

#include <mqtt/MQTT.h>
#include <mqtt/MQTTReporterInterface.h>
#include <mqtt/MQTTPublishInterface.h>

#include <PeriodicCounter.h>
#include <viewable_stringbuf.h>

#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace heating {

using namespace std::string_view_literals;

class RoomsReporter final : public ib::mqtt::MQTTReporterInterface {
public:
	using getRoomCount_t = std::function<std::size_t()>;
	using getRoomStatus_t = std::function<void(std::ostream &)>;

	RoomsReporter(std::shared_ptr<ib::logger::LoggerInterface> log, getRoomCount_t getRoomsCount, getRoomStatus_t getRoomsStatus) : log_(std::move(log)), getRoomsCount_(std::move(getRoomsCount)), getRoomsStatus_(std::move(getRoomsStatus)) { if (log_) logFeature_ = log_->addFeature("MQTT rooms"); }

	void getStatus(std::ostream &ss) const override {
		ss << "{\"rooms\": ";
		getRoomsStatus_(ss);
		ss << "}";
	}

	void publishHADiscovery(ib::mqtt::MQTTPublishInterface &publish) override {
		auto roomCount = getRoomsCount_();
		for (size_t room = 0; room < roomCount; ++room) {
			auto roomId = roomEntityId(room);
			publish.publishAutoDiscoveryBinarySensor("room_data"sv, roomId + "_enabled", "Heating enabled"sv, "enabledState"sv, ""sv, ""sv);
			publish.publishAutoDiscoveryBinarySensor("room_data"sv, roomId + "_heating", "Room is being heated"sv, "isBeingHeatedState"sv, ""sv, ""sv);
			publish.publishAutoDiscoverySensor("room_data"sv, roomId + "_curr_temp", "Current temperature"sv, "currentTemp"sv, "/ 100"sv, "°C"sv, "measurement"sv, "temperature"sv, {});
			publish.publishAutoDiscoverySensor("room_data"sv, roomId + "_temp_set", "Temperature set"sv, "tempSet"sv, "/ 100"sv, "°C"sv, "measurement"sv, "temperature"sv, {});
			publish.publishAutoDiscoverySensor("room_data"sv, roomId + "_name", "Room name"sv, "name"sv, ""sv, ""sv, ""sv, ""sv, ""sv);
			publish.publishAutoDiscoverySensor("room_data"sv, roomId + "_battery", "Battery level"sv, "batteryLevel"sv, ""sv, "%"sv, "measurement"sv, "battery"sv, {});
			publish.publishAutoDiscoverySensor("room_data"sv, roomId + "_humidity", "Humidity"sv, "currentHumidity"sv, "/ 100"sv, "%"sv, "measurement"sv, "humidity"sv, {});
		}
	}

	void publishStateTopic(ib::mqtt::MQTTPublishInterface &publish, uint16_t intervalSecs) override {
		lastMqttPublishCounter_.setIntervalMs(intervalSecs * 1000);
		if (!lastMqttPublishCounter_.durationPassed()) {
			DBGLOGFD(log_, logFeature_, "Skipping MQTT publish, interval not passed yet. Time to wait: %ld ms\n", lastMqttPublishCounter_.getTimeToWaitMs());
			return;
		}

		ib::viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		getStatus(ss);

		publish.publishStateTopic("room_data"sv, payloadBuf.view(), false);
	}

private:
	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_{};
	static std::string roomEntityId(size_t roomNo) { return "opth_room_" + std::to_string(roomNo); }

	ib::PeriodicCounter lastMqttPublishCounter_{1000};
	getRoomCount_t getRoomsCount_;
	getRoomStatus_t getRoomsStatus_;
};

class DeviceStatusReporter final : public ib::mqtt::MQTTReporterInterface {
public:
	explicit DeviceStatusReporter(std::shared_ptr<ib::logger::LoggerInterface> log) : log_(std::move(log)) { if (log_) logFeature_ = log_->addFeature("MQTT device status"); }

	void getStatus(std::ostream &ss) const override {
		ss << "{\"mem_free\":" << ESP.getFreeHeap() << ",";
		ss << "\"mem_min_free\":" << ESP.getMinFreeHeap() << ",";
		ss << "\"mem_max_alloc\":" << ESP.getMaxAllocHeap() << ",";
		ss << "\"temperature\":" << heating::rtcGetTemp() << ",";
		ss << "\"cpu_temperature\":" << temperatureRead() << ",";
		ss << "\"uptime\":" << millis() / 1000;
		ss << "}";
	}

	void publishHADiscovery(ib::mqtt::MQTTPublishInterface &publish) override {
		constexpr std::string_view unit_byte = "B"sv;
		publish.publishAutoDiscoverySensor("device_status"sv, "opth_memory_free"sv, "OpenThermostat free memory"sv, "mem_free"sv, ""sv, unit_byte, "measurement"sv, "data_size"sv, {});
		publish.publishAutoDiscoverySensor("device_status"sv, "opth_memory_min_free"sv, "OpenThermostat minimum free memory"sv, "mem_min_free"sv, ""sv, unit_byte, "measurement"sv, "data_size"sv, {});
		publish.publishAutoDiscoverySensor("device_status"sv, "opth_memory_max_alloc"sv, "OpenThermostat max allocable memory block"sv, "mem_max_alloc"sv, ""sv, unit_byte, "measurement"sv, "data_size"sv, {});
		publish.publishAutoDiscoverySensor("device_status"sv, "opth_temperature"sv, "OpenThermostat RTC temperature inside box"sv, "temperature"sv, ""sv, "°C"sv, "measurement"sv, "temperature"sv, {});
		publish.publishAutoDiscoverySensor("device_status"sv, "opth_cpu_temperature"sv, "OpenThermostat ESP32 CPU temperature"sv, "cpu_temperature"sv, ""sv, "°C"sv, "measurement"sv, "temperature"sv, {});
		publish.publishAutoDiscoverySensor("device_status"sv, "opth_uptime"sv, "OpenThermostat device uptime"sv, "uptime"sv, ""sv, "s"sv, "total_increasing"sv, "duration"sv, {});
	}

	void publishStateTopic(ib::mqtt::MQTTPublishInterface &publish, uint16_t intervalSecs) override {
		lastMqttPublishCounter_.setIntervalMs(intervalSecs * 1000);
		if (!lastMqttPublishCounter_.durationPassed()) {
			DBGLOGFD(log_, logFeature_, "Skipping MQTT publish, interval not passed yet. Time to wait: %ld ms\n", lastMqttPublishCounter_.getTimeToWaitMs());
			return;
		}

		ib::viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		getStatus(ss);

		publish.publishStateTopic("device_status"sv, payloadBuf.view(), false);
	}

private:
	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_{};
	ib::PeriodicCounter lastMqttPublishCounter_{1000};
};

class EmsMetricsReporter final : public ib::mqtt::MQTTReporterInterface {
public:
	using getEmsMetrics_t = std::function<void(std::ostream &)>;

	EmsMetricsReporter(std::shared_ptr<ib::logger::LoggerInterface> log, getEmsMetrics_t getEmsMetrics) : log_(std::move(log)), getEmsMetrics_(std::move(getEmsMetrics)) { if (log_) logFeature_ = log_->addFeature("MQTT EMS metrics"); }

	void getStatus(std::ostream &ss) const override { getEmsMetrics_(ss); }

	void publishHADiscovery(ib::mqtt::MQTTPublishInterface &publish) override {
		publish.publishAutoDiscoverySensor("ems_metrics"sv, "opth_energy"sv, "Total energy consumption"sv, "totalEnergyUsedKwh"sv, ""sv, "kWh"sv, "total"sv, "energy"sv, {});
		publish.publishAutoDiscoverySensor("ems_metrics"sv, "opth_energy_warm_water"sv, "Energy used for warm water heating"sv, "warmWaterEnergyUsedKwh"sv, ""sv, "kWh"sv, "total"sv, "energy"sv, {});
		publish.publishAutoDiscoverySensor("ems_metrics"sv, "opth_energy_heating"sv, "Energy used for space heating"sv, "heatingEnergyUsedKwh"sv, ""sv, "kWh"sv, "total"sv, "energy"sv, {});
		publish.publishAutoDiscoverySensor("ems_metrics"sv, "opth_warm_water_usage"sv, "Warm water usage"sv, "warmWaterUsage"sv, ""sv, "L"sv, "measurement"sv, ""sv, ""sv);
		publish.publishAutoDiscoverySensor("ems_metrics"sv, "opth_warm_water_avg_flow"sv, "Average flow of warm water"sv, "warmWaterAvgFlow"sv, ""sv, "L/min"sv, "measurement"sv, ""sv, ""sv);
		publish.publishAutoDiscoverySensor("ems_metrics"sv, "opth_outdoor_temperature"sv, "Outdoor temperature"sv, "outdoorTemperature"sv, ""sv, "°C"sv, "measurement"sv, "temperature"sv, {});
	}

	void publishStateTopic(ib::mqtt::MQTTPublishInterface &publish, uint16_t intervalSecs) override {
		lastMqttPublishCounter_.setIntervalMs(intervalSecs * 1000);
		if (!lastMqttPublishCounter_.durationPassed()) {
			DBGLOGFD(log_, logFeature_, "Skipping MQTT publish, interval not passed yet. Time to wait: %ld ms\n", lastMqttPublishCounter_.getTimeToWaitMs());
			return;
		}

		ib::viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		getStatus(ss);

		publish.publishStateTopic("ems_metrics"sv, payloadBuf.view(), false);
	}

private:
	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_{};
	ib::PeriodicCounter lastMqttPublishCounter_{1000};
	getEmsMetrics_t getEmsMetrics_;
};

class MQTT {
public:
	using getRoomStatus_t = std::function<void(std::ostream &)>;
	using getRoomCount_t = std::function<std::size_t()>;
	using getEmsMetrics_t = std::function<void(std::ostream &)>;

	MQTT(std::shared_ptr<ib::logger::LoggerInterface> log, getRoomCount_t getRoomsCount, getRoomStatus_t getRoomsStatus, getEmsMetrics_t getEmsMetrics) : log_(std::move(log)) {
		if (log_) {
			static const auto id = log_->addFeature("MQTT");
			logFeature_ = id;
		}
		auto mqttConfig = config::getMqttConfig();
		DBGLOGFD(log_, logFeature_, "Enabled: %d\n", mqttConfig.enabled);
		DBGLOGFD(log_, logFeature_, "%s:%d\n", mqttConfig.brokerAddress.c_str(), mqttConfig.brokerPort);
		DBGLOGFD(log_, logFeature_, "publish interval %d, keep alive inteval: %d\n", mqttConfig.interval, mqttConfig.keepAlive);
		DBGLOGFD(log_, logFeature_, "clientId '%s', base: '%s'\n", mqttConfig.clientId.c_str(), mqttConfig.base.c_str());

		if (!mqttConfig.enabled) {
			return;
		}

		ib::mqtt::MqttConfig libConfig;
		libConfig.enabled = mqttConfig.enabled;
		libConfig.brokerAddress = mqttConfig.brokerAddress;
		libConfig.brokerPort = mqttConfig.brokerPort;
		libConfig.username = mqttConfig.username;
		libConfig.password = mqttConfig.password;
		libConfig.base = mqttConfig.base.empty() ? "open_thermostat" : mqttConfig.base;
		libConfig.clientId = mqttConfig.clientId.empty() ? libConfig.base : mqttConfig.clientId;
		libConfig.keepAlive = mqttConfig.keepAlive;
		libConfig.interval = mqttConfig.interval;

		ib::mqtt::MQTT::HomeAssistantDeviceInfo deviceInfo;
		deviceInfo.name = "OpenThermostat";
		deviceInfo.model = "OpenThermostat";
		deviceInfo.manufacturer = "intuibase";
		deviceInfo.swVersion = "1.0.0";

		reporters_.emplace_back(std::make_shared<RoomsReporter>(log_, std::move(getRoomsCount), std::move(getRoomsStatus)));
		reporters_.emplace_back(std::make_shared<DeviceStatusReporter>(log_));
		reporters_.emplace_back(std::make_shared<EmsMetricsReporter>(log_, std::move(getEmsMetrics)));

		mqtt_ = std::make_shared<ib::mqtt::MQTT>(log_, libConfig, deviceInfo, reporters_);
	}

	void loop() {
		if (mqtt_) {
			mqtt_->loop();
		}
	}

private:
	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_{};
	std::shared_ptr<ib::mqtt::MQTT> mqtt_;
	std::vector<std::shared_ptr<ib::mqtt::MQTTReporterInterface>> reporters_;
};
}
