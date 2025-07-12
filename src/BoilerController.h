#pragma once

#include "config.h"
#include "TimeHelpers.h"

#include "Logger.h"
#include <sstream>
#include <algorithm>
#include <set>
#include <functional>
#include <mutex>
#include <optional>

namespace heating {

class BoilerController {
public:
	using boilerHeatingTemperatureOverride_t = std::optional<uint8_t>;
	using getOutdoorTemp_t = std::function<int16_t()>;
	using emsChangeBoilerState_t = std::function<void(bool, uint8_t)>;
	using emsSetHeatingTemperature_t = std::function<void(uint8_t)>;

	BoilerController(config::BoilerConfig const &config, getOutdoorTemp_t getOutdoorTemp, emsChangeBoilerState_t emsChangeBoilerState, emsSetHeatingTemperature_t emsSetHeatingTemperature) : config_(config), getOutdoorTemp_(getOutdoorTemp), emsChangeBoilerState_(emsChangeBoilerState), emsSetHeatingTemperature_(emsSetHeatingTemperature), valves_(config::getValvePins()) {
	}

	bool valvePreheating_ = false;
	unsigned long lastPreheatTime_ = 0;

	void startBoilerOrContinue(bool shouldStartBoiler, bool shouldBoilerContinue, boilerHeatingTemperatureOverride_t boilerHeatingTemperatureOverride) {
		DBGLOGBOILER("Current boiler state: %d, should start: %d, should continue: %d, boiler heating temp override: %d\n", isBoilerStarted(), shouldStartBoiler, shouldBoilerContinue, boilerHeatingTemperatureOverride.value_or(0));

		if (valvePreheating_ && heating::millisDurationPassed(lastPreheatTime_, config_.boiler.valvePreheatingDelay * 1000)) {
			DBGLOGBOILER("Finished valve preheating. Changing boiler state to: %s\n", (shouldStartBoiler || shouldBoilerContinue) ? "enabled" : "disabled");
			valvePreheating_ = false;
			changeBoilerState(shouldStartBoiler || shouldBoilerContinue, boilerHeatingTemperatureOverride);
			return;
		}

		if (shouldStartBoiler) {
			if (config_.boiler.valvePreheatingDelay > 0 && !isBoilerStarted()) {
				if (valvePreheating_) {
					DBGLOGBOILER("Valve preheating in progress. Delay %zums\n", (config_.boiler.valvePreheatingDelay * 1000) - (millis() - lastPreheatTime_));
					return;
				}
				valvePreheating_ = true;
				lastPreheatTime_ = millis();
				DBGLOGBOILER("Started valve preheating. Boiler start delayed by %zums\n", config_.boiler.valvePreheatingDelay * 1000);
				return;
			}
			changeBoilerState(true, boilerHeatingTemperatureOverride);
			return;
		}

		if (!shouldBoilerContinue && isBoilerStarted()) {
			changeBoilerState(false, boilerHeatingTemperatureOverride);
			return;
		}

		if (!shouldStartBoiler && !shouldBoilerContinue) {
			changeBoilerState(false, boilerHeatingTemperatureOverride);
			return;
		}

		// for onoff_outdoor we need to update heating temperature in case it changed
		if (config_.boiler.controlMode == config::BoilerConfig::controlMode_t::ems || config_.boiler.controlMode == config::BoilerConfig::controlMode_t::onoff_outdoor) {
			changeBoilerState(isBoilerStarted(), boilerHeatingTemperatureOverride);
		}
	}

	void handleValves(std::set<uint8_t> const &valvesToClose) {
		if (!isBoilerStarted()) {
			openAllValves();
			return;
		}

		DBGLOGBOILER("valves to close count: %zu CLOSED: ", valvesToClose.size());

		for (size_t valve = 0; valve < valves_.size(); ++valve) {
			bool close = valvesToClose.find(valve) != std::end(valvesToClose);
			handleValve(valve, close);
			if (debug::debug.debugBoilerController) {
				logger.printf("%d", close);
			}
		}
		if (debug::debug.debugBoilerController) {
			logger.println();
		}
	}

	void getStatus(std::ostream &ss) const {
		ss << "{\"boiler\": " << (isBoilerStarted() ? "true" : "false") << ", \"valvesOpened\": [";
		for (size_t valve = 0; valve < valvesStates_.size(); ++valve) {
			ss << (valvesStates_[valve] ? "true" : "false");
			if (valve + 1 < valvesStates_.size()) {
				ss << ", ";
			}
		}
		ss << "]";
		if (currentOutdoorTemperature_) {
			ss << ", \"outdoorTemperature\": " << currentOutdoorTemperature_.value();
		}
		if (currentHeatingTemperature_) {
			ss << ", \"heatingTemperature\": " << currentHeatingTemperature_.value();
		}
		ss << "}";
	}

	std::string getStatus() const {

		std::stringstream ss;
		getStatus(ss);
		return ss.str();
	}

private:
	void openAllValves() {
		for (size_t valve = 0; valve < valves_.size(); ++valve) {
			handleValve(valve, false);
		}
	}

	void handleValve(uint8_t nr, bool closed) {
		std::lock_guard<std::mutex> lock(mutex_);
		auto pin = valves_[nr];
		pinMode(pin, OUTPUT);

		digitalWrite(pin, closed ? LOW : HIGH);
		valvesStates_[nr] = !closed;
		//		DBGLOGBOILER("Handle valve %d state: %s coil %s \n", nr, !closed ? "open " : "close", closed ? "on" : "off");
	}

	void changeBoilerState(bool enabled, boilerHeatingTemperatureOverride_t boilerHeatingTemperatureOverride) {
		std::lock_guard<std::mutex> lock(mutex_);

		currentBoilerState_ = enabled;
		currentHeatingTemperature_ = {};
		currentOutdoorTemperature_ = {};

		DBGLOGBOILER("changeBoilerState to: %d, control mode: %d\n", enabled, config_.boiler.controlMode);

		if (config_.boiler.controlMode == config::BoilerConfig::controlMode_t::onoff || config_.boiler.controlMode == config::BoilerConfig::controlMode_t::onoff_outdoor) {

			if (config_.boiler.controlMode == config::BoilerConfig::controlMode_t::onoff_outdoor) {
				currentOutdoorTemperature_ = getOutdoorTemp_();
				currentHeatingTemperature_ = getHeatingTemperature(currentOutdoorTemperature_.value());

				if (boilerHeatingTemperatureOverride.has_value()) {
					currentHeatingTemperature_ = std::max(currentHeatingTemperature_.value(), static_cast<int16_t>(boilerHeatingTemperatureOverride.value() * 100));
				}

				emsSetHeatingTemperature(currentHeatingTemperature_.value() / 100);
			}

			pinMode(boilerPin_, OUTPUT);
			DBGLOGBOILER("ON/OFF BOILER pin: %d enabled: %d\n", boilerPin_, enabled);
			digitalWrite(boilerPin_, enabled ? LOW : HIGH);
		} else if  (config_.boiler.controlMode == config::BoilerConfig::controlMode_t::ems) {
			currentOutdoorTemperature_ = getOutdoorTemp_();
			currentHeatingTemperature_ = getHeatingTemperature(currentOutdoorTemperature_.value());

			if (boilerHeatingTemperatureOverride.has_value()) {
				currentHeatingTemperature_ = std::max(currentHeatingTemperature_.value(), static_cast<int16_t>(boilerHeatingTemperatureOverride.value() * 100));
			}

			DBGLOGBOILER("emsChangeBoilerState enabled: %d heatingTemp: %d\n", enabled, currentHeatingTemperature_.value() / 100);

			emsSetHeatingTemperature(config_.heatingCurve.maxHeatingCurveTemp);

			emsChangeBoilerState_(enabled, currentHeatingTemperature_.value() / 100);
		}
	}

	bool isBoilerStarted() const { return currentBoilerState_; }

	int16_t getHeatingTemperature(int16_t outdoorTemperature) {
		static constexpr std::array<int8_t, 9> hcOutdoorAxis{20, 15, 10, 5, 0, -5, -10, -15, -20};

		for (size_t idx = 0; idx < hcOutdoorAxis.size(); ++idx) {

			if (hcOutdoorAxis[idx] * 100 <= outdoorTemperature) {
				if (idx == 0) {
					DBGLOGBOILER("getHeatingTemperature: %d , hcOutdoorAxis: %d, hcHeatTemp: %d\n", static_cast<int>(outdoorTemperature), static_cast<int>(hcOutdoorAxis[idx] * 100), static_cast<int>(config_.heatingCurve.heatingCurve[idx]));
					return config_.heatingCurve.heatingCurve[idx] * 100;
				} else {
					float slope = static_cast<float>(config_.heatingCurve.heatingCurve[idx] - config_.heatingCurve.heatingCurve[idx - 1]) / static_cast<float>(hcOutdoorAxis[idx] - hcOutdoorAxis[idx - 1]); // (y2-y1)/(x2-x1)
					float intercept = config_.heatingCurve.heatingCurve[idx - 1] * 100 - (slope * hcOutdoorAxis[idx - 1] * 100); // y1 - (slope * x1)
					int16_t temp = slope * outdoorTemperature + intercept;
					DBGLOGBOILER("getHeatingTemperature outdoor: %d , hcOutdoorAxis: %d, hcHeatTemp: %d Calculated: %d\n", static_cast<int>(outdoorTemperature), static_cast<int>(hcOutdoorAxis[idx] * 100), static_cast<int>(config_.heatingCurve.heatingCurve[idx]), static_cast<int>(temp));
					return temp;
				}
			}
		}

		DBGLOGBOILER("getHeatingTemperature outdoor: %d , heat temp: %d\n", static_cast<int>(outdoorTemperature), static_cast<int>(config_.heatingCurve.heatingCurve[8]));
		return config_.heatingCurve.heatingCurve[8];
	}

	void emsSetHeatingTemperature(uint8_t temperature) {
		DBGLOGBOILER("emsSetHeatingTemperature to: %d\n", temperature);
		emsSetHeatingTemperature_(temperature);
	}


	config::BoilerConfig const &config_;
	getOutdoorTemp_t getOutdoorTemp_;
	emsChangeBoilerState_t emsChangeBoilerState_;
	emsSetHeatingTemperature_t emsSetHeatingTemperature_;
	config::valves_t valves_;
	std::array<bool, VALVE_COUNT> valvesStates_ = {{true, true, true, true, true, true, true, true}}; // not needed, for debugging? UI?
	uint8_t boilerPin_ = config::getBoilerPin();


	bool currentBoilerState_ = false; // true - heating
	std::optional<int16_t> currentHeatingTemperature_ = 0;
	std::optional<int16_t> currentOutdoorTemperature_ = 0;

	std::mutex mutex_;
};

}
