#pragma once

#include "BeaconBleAddress.h"
#include "BoilerController.h"
#include "BeaconTemperatureReader.h"
#include "OpenWeather.h"
#include "PeriodicCounter.h"
#include "EmsController.h"
#include "EmsMetrics.h"
#include "MQTT.h"

#include "Logger.h"
#include <atomic>
#include <set>
#include <iomanip>
#include <mutex>
#include "Arduino.h"
#include "config.h"


namespace heating {

class HeatingController {
public:
	using boilerHeatingTemperatureOverride_t = BoilerController::boilerHeatingTemperatureOverride_t;

	HeatingController() : openWeather_(config::getOpenWeatherConfig()), currentProgram_(config::getCurrentProgram()), rooms_(config::getRoomsConfig(currentProgram_)) {
		bluetoothScan_ = true;
		lastReadTemperatureCounter_.notifyNow();
	}

	~HeatingController() {
		vTaskDelete(nullptr);
	}

	void operate() {
		bool shouldStartBoiler = false;
		bool shouldBoilerContinue = false;
		boilerHeatingTemperatureOverride_t boilerHeatingTempOverride;

		std::set<uint8_t> valvesWhichShouldBeClosed;

		{ // mutex scope
		std::lock_guard<std::mutex> lock(roomsAccessMutex_);
		for (auto &room : rooms_) {
			if (room.isEnabled()) {
				auto [roomStatus, roomBoilerHeatingTempOverride] = room.shouldStartBoilerAndHeat();

				shouldStartBoiler = shouldStartBoiler || roomStatus == Room::TemperatureStatus::START_HEATING;
				shouldBoilerContinue = shouldBoilerContinue || roomStatus == Room::TemperatureStatus::CONTINUE_HEATING;

				if (roomBoilerHeatingTempOverride.has_value()) {
					boilerHeatingTempOverride = std::max(boilerHeatingTempOverride.value_or(0), roomBoilerHeatingTempOverride.value()); // use highest boiler supply temperature from overrides
				}

				// valve should be closed only if temperature is in upper/lower margin - in case of no samples, valve should remain open but should not trigger or continue heating
				if (roomStatus == Room::TemperatureStatus::TEMPERATURE_OK) {
					if (debug::debug.debugHeatingController) {
						DBGLOGHC("  adding valves to close for room %s valves: ", room.name_.c_str());
						for (auto valve : room.valves_) {
							logger.printf("%d ", valve);
						}
						logger.println("");
					}

					valvesWhichShouldBeClosed.insert(room.valves_.begin(), room.valves_.end());
				}
			} else { // room is diabled - let's heat it because we don't know what happened
				//				valvesWhichShouldBeOpened.insert(room.valves_.begin(), room.valves_.end());
			}

			if (bluetoothScan_) {
				resetIfNoDataForLongTime();
			}
		}
		}
		boiler_.handleValves(valvesWhichShouldBeClosed);
		boiler_.startBoilerOrContinue(shouldStartBoiler, shouldBoilerContinue, boilerHeatingTempOverride);


		DBGLOGHC("%s\n", boiler_.getStatus().c_str());

		if (bluetoothScan_) {
			tempReader_.triggerScan();
		}

		openWeather_.operate();

		mqtt_.operate();
	}

	void loop() {
		mqtt_.loop();
		ems_.loop();
	}

	void getBoilerStatus(std::ostream &ss) const {
		boiler_.getStatus(ss);
	}

	std::string getBoilerStatus() const {
		return boiler_.getStatus();
	}

	void getEMSStatus(std::ostream &ss) const {
		ems_.getStatus(ss);
	}

	std::string getEMSStatus() const {
		return ems_.getStatus();
	}

	void getEMSBoilerParams(std::ostream &ss) const {
		ems_.getBoilerParams(ss);
	}

	std::string getEMSBoilerParams() const {
		return ems_.getBoilerParams();
	}

	void getFullStatus(std::ostream &ss) const {
		ss << "{";
		ss << "\"rooms\": ";
		getRoomsStatus(ss);
		ss << ",";
		{
			std::lock_guard<std::mutex> lock(roomsAccessMutex_);
			ss << "\"activeProgram\": \"" << currentProgram_ << "\",";
		}
		ss << "\"boiler\": "; boiler_.getStatus(ss); ss << ",";
		ss << "\"ems\": "; ems_.getStatus(ss); ss << ",";
		ss << "\"openweather\": " << openWeather_.getStatus() << ",";

		struct tm timeinfo;
		getLocalTime(&timeinfo);

		ss << "\"time\": \"" << std::setfill('0') << std::setw(2) << timeinfo.tm_hour << ":"
			<< std::setfill('0') << std::setw(2) << timeinfo.tm_min << ":"
			<< std::setfill('0') << std::setw(2) << timeinfo.tm_sec << "\",";
		ss << "\"date\": \"" << std::setfill('0') << std::setw(4) << timeinfo.tm_year + 1900 << "-"
			<< std::setfill('0') << std::setw(2) << timeinfo.tm_mon + 1 << "-"
			<< std::setfill('0') << std::setw(2) << timeinfo.tm_mday << "\",";

		ss << "\"weekDay\": " << timeinfo.tm_wday << ",";
		ss << "\"uptime\": " << millis()/1000;


		ss << "}";
	}


	std::string getFullStatus() const {
		std::stringstream ss;
		getFullStatus(ss);
		return ss.str();
	}

	std::string getRoomsStatus() const {
		std::stringstream ss;
		getRoomsStatus(ss);
		return ss.str();
	}

	size_t getRoomsStatus(std::ostream &ss) const {
		ss << "[";
		size_t r = 0;
		std::lock_guard<std::mutex> lock(roomsAccessMutex_);

		for (auto const &room : rooms_) {
			room.getStatus(ss);
			r++;
			if (r != rooms_.size()) {
				ss << ",";
			}
		}
		ss << "]";

		return rooms_.size();
	}

	size_t getRoomsCount() {
		std::lock_guard<std::mutex> lock(roomsAccessMutex_);
		return rooms_.size();
	}

	bool setRoomTemporaryTemperature(std::string const &name, int16_t temperature, uint32_t validSeconds) {
		std::lock_guard<std::mutex> lock(roomsAccessMutex_);

		for (auto &room : rooms_) {
			if (room.name_ == name) {
				room.createTemporaryOverride(temperature, validSeconds);
				return true;
			}
		}
		DBGLOGHC("setRoomTemporaryTemperature. No room '%s' found\n", name.c_str());

		return false;
	}

	void getDevicesFound(std::ostream &ss) {
		std::lock_guard<std::mutex> lock(roomsAccessMutex_);

		DBGLOGHC("getDevicesFound %zu\n", devicesFound_.size());

		ss << "[";
		for (auto it = devicesFound_.begin(); it != devicesFound_.end(); ++it) {
			if (it != devicesFound_.begin()) {
				ss << ", ";
			}
		    #define GETHEX_STR(val) std::hex  << std::setfill('0') << std::setw(2) << (int)val
			ss << "\"" << GETHEX_STR(it->at(0)) << ':' << GETHEX_STR(it->at(1)) << ':' << GETHEX_STR(it->at(2)) << ':' << GETHEX_STR(it->at(3)) << ':' << GETHEX_STR(it->at(4)) << ':' << GETHEX_STR(it->at(5)) << "\"";
		}
		ss << "]";
	}

	void stopBluetoothScan() {
		bluetoothScan_ = false;
	}

	void waitUntilBluetoothScanFinishAndDeinitBLE() {
		while (tempReader_.isScanPending()) {
			delay(1);
		}
		tempReader_.shutDownBLE();
	}

	void reloadConfiguration() {
		DBGLOGHC("reloadConfiguration%s\n", "");
		std::lock_guard<std::mutex> lock(roomsAccessMutex_);
		currentProgram_ = config::getCurrentProgram();
		rooms_ = config::getRoomsConfig(currentProgram_);
	}

private:

	void pushTemperatureData(BleAddress_t address, std::optional<int16_t> temperature, std::optional<int16_t> humidity, std::optional<int8_t> battery) {
		std::lock_guard<std::mutex> lock(roomsAccessMutex_);

		devicesFound_.emplace(address);

		auto room = std::find_if(std::begin(rooms_), std::end(rooms_), [&adr = address](auto const &room) { return (adr == room.sensorAddress_); });
		if (room == std::end(rooms_)) {
			DBGLOGHC("No room for address '" PRiBleAddress "'\n", PRaBleAddress(address));
			return;
		}

		DBGLOGHC("push '%s', " PRiBleAddress " temp: %d battery: %d%% BTC MinPeekStack: %d\n", room->name_.c_str(), PRaBleAddress(address), temperature.value_or(std::numeric_limits<decltype(temperature)::value_type>::min()), battery.value_or(std::numeric_limits<decltype(battery)::value_type>::min()), uxTaskGetStackHighWaterMark(nullptr));

		if (temperature.has_value()) {
			room->storeTemperature(temperature.value());
			lastReadTemperatureCounter_.notifyNow();
		}

		if (humidity.has_value()) {
			room->storeHumidity(humidity.value());
		}

		if (battery.has_value()) {
			room->storeBattery(battery.value());
		}

	}

	void resetIfNoDataForLongTime() {
		if (lastReadTemperatureCounter_.durationPassed()) {
			logger.println("RESTARTING DUE TO NO DATA FOR OVER 5m");
			ESP.restart();
		}
	}

	int16_t getOutdoorTemperature() {
		static constexpr int16_t outdoorTemperatureIfInvalidRead = -2000;

		std::optional<int16_t> temp;

		if (boilerConfig_.boiler.outdoorSensor == config::BoilerConfig::outdoorSensor_t::openweather) {
			temp = openWeather_.getTemperature();
		} else if (boilerConfig_.boiler.outdoorSensor == config::BoilerConfig::outdoorSensor_t::ems) {
			ems::EmsBoilerState &boilerState = ems_.getBoilerState();
			std::lock_guard<std::mutex> lock(boilerState.mutex);
			temp = boilerState.outdoorTemperature ;
		} else {
			// no temp sensor
		}

		if (!temp.has_value()) { // try to get any valid temperature
			DBGLOGHC("Outdoor temperature is invalid. Trying to get it from OpenWeather or EMS\n", "");
			auto owtemp = openWeather_.getTemperature();

			ems::EmsBoilerState &boilerState = ems_.getBoilerState();
			std::lock_guard<std::mutex> lock(boilerState.mutex);

			temp = owtemp.has_value() ? owtemp : boilerState.outdoorTemperature;
		}

		if (!temp.has_value()) {
			DBGLOGHC("Outdoor temperature is invalid. Setting to -20C\n", "");
			return outdoorTemperatureIfInvalidRead;
		} else {
			return temp.value();
		}
	}

	std::atomic_bool bluetoothScan_;
	BeaconTemperatureReader tempReader_{[this](BleAddress_t address, std::optional<int16_t> temperature, std::optional<int16_t> humidity, std::optional<int8_t> battery) { pushTemperatureData(std::move(address), temperature, humidity, battery); }};
	OpenWeather openWeather_;
	ems::EmsController ems_;
	config::BoilerConfig boilerConfig_{config::getBoilerConfig()};

	BoilerController boiler_ {boilerConfig_,
		[this]() { return getOutdoorTemperature(); },
		[&ems = ems_](bool enabled, uint8_t flowTempSet) {
			ems.changeBoilerState(enabled, flowTempSet);
		},
		[&ems = ems_](uint8_t heatingTemperature) {
			ems.setHeatingTemperature(heatingTemperature);
		}
	};
	std::string currentProgram_;
	std::vector<heating::Room> rooms_;
	ib::PeriodicCounter lastReadTemperatureCounter_{5 * 60 * 1000}; // 5 mins in ms
	BeaconTemperatureReader::BleDevices_t devicesFound_;
	mutable std::mutex roomsAccessMutex_;

	ems::EmsMetrics emsMetrics_{[this](uint16_t telegramId, std::function<void(heating::ems::EmsTelegram const &)> processor) { ems_.registerTelegramProcessor(telegramId, processor); }};

	MQTT mqtt_{
		[this]() {return getRoomsCount();},
		[this](std::ostream &ss) { getRoomsStatus(ss);},
		[this](std::ostream &ss) { emsMetrics_.getMetrics(ss);}
		};
};



}
