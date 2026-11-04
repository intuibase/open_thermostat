#pragma once
#include "Logging.h"

#include "config.h"
#include "GpioPort.h"
#include "BoilerSupply.h"
#include <logger/LoggerInterface.h>
#include <sstream>
#include <algorithm>
#include <set>
#include <functional>
#include <chrono>
#include <mutex>
#include <optional>
#include <memory>
#include <vector>

namespace heating {

class BoilerController {
public:
	using clock_t = std::chrono::steady_clock;
	using now_t = std::function<clock_t::time_point()>;
	using boilerHeatingTemperatureOverride_t = std::optional<uint8_t>;
	using getOutdoorTemp_t = std::function<std::optional<int16_t>()>;
	using emsChangeBoilerState_t = std::function<void(bool, uint8_t)>;
	using emsSetHeatingTemperature_t = std::function<void(uint8_t)>;

	BoilerController(std::shared_ptr<ib::logger::LoggerInterface> log, config::BoilerConfig const &config, getOutdoorTemp_t getOutdoorTemp, emsChangeBoilerState_t emsChangeBoilerState, emsSetHeatingTemperature_t emsSetHeatingTemperature,
		std::unique_ptr<gpio::GpioPort> boilerPort, std::vector<std::unique_ptr<gpio::GpioPort>> valvePorts, std::vector<std::string> valveLabels, now_t now = [] { return clock_t::now(); })
		: log_(std::move(log)), config_(config), getOutdoorTemp_(getOutdoorTemp), emsChangeBoilerState_(emsChangeBoilerState), emsSetHeatingTemperature_(emsSetHeatingTemperature)
		, boilerPort_(std::move(boilerPort)), valvePorts_(std::move(valvePorts))
		, valveLabels_(std::move(valveLabels))
		, valvesStates_(valvePorts_.size(), true), now_(std::move(now)) {
		if (log_) {
			logFeature_ = log_->addFeature("Boiler");
		}
		boilerPort_->initOutput();
		for (auto &vp : valvePorts_) {
			vp->initOutput();
		}
	}

	SupplyDecision previewSupply() const {
		return resolveSupply(config_, outdoorReading());
	}

	SupplyDecision appliedSupplyDecision() const {
		std::lock_guard<std::mutex> lock(mutex_);
		auto decision = supplyDecision_;
		decision.requested = appliedSupply_.requested;
		decision.boosted = appliedSupply_.boosted;
		return decision;
	}

	bool isSupplyBoostApplied() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return currentBoilerState_ && !manualTestActive_ && appliedSupply_.boosted;
	}

	bool isSupplyBaselineUnknown() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return currentBoilerState_ && config_.boiler.controlMode != config::BoilerConfig::controlMode_t::onoff && !supplyDecision_.base;
	}

	void setOutdoorProvider(std::function<OutdoorReading()> provider) { outdoorProvider_ = std::move(provider); }

	void startBoilerOrContinue(bool shouldStartBoiler, bool shouldBoilerContinue, boilerHeatingTemperatureOverride_t boilerHeatingTemperatureOverride) {
		if (isManualTestActive())
			return; // manual test overrides normal operation

		DBGLOGFD(log_, logFeature_, "Current boiler state: %d, should start: %d, should continue: %d, boiler heating temp override: %d\n", isBoilerStarted(), shouldStartBoiler, shouldBoilerContinue, boilerHeatingTemperatureOverride.value_or(0));

		if (!isBoilerStarted()) {
			std::lock_guard<std::mutex> lock(mutex_);
			supplyDecision_ = resolveSupply(config_, outdoorReading(), boilerHeatingTemperatureOverride);
		}
		if (!shouldStartBoiler && !shouldBoilerContinue) valvePreheating_ = false;

		if (valvePreheating_ && (now_() - lastPreheatTime_ >= std::chrono::seconds(config_.boiler.valvePreheatingDelay))) {
			DBGLOGFD(log_, logFeature_, "Finished valve preheating. Changing boiler state to: %s\n", (shouldStartBoiler || shouldBoilerContinue) ? "enabled" : "disabled");
			valvePreheating_ = false;
			changeBoilerState(shouldStartBoiler || shouldBoilerContinue, boilerHeatingTemperatureOverride);
			return;
		}

		if (shouldStartBoiler) {
			if (config_.boiler.valvePreheatingDelay > 0 && !isBoilerStarted()) {
				if (valvePreheating_) {
					auto remaining = std::chrono::seconds(config_.boiler.valvePreheatingDelay) - (now_() - lastPreheatTime_);
					DBGLOGFD(log_, logFeature_, "Valve preheating in progress. Delay %lldms\n", std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
					return;
				}
				valvePreheating_ = true;
				lastPreheatTime_ = now_();
				DBGLOGFD(log_, logFeature_, "Started valve preheating. Boiler start delayed by %ds\n", config_.boiler.valvePreheatingDelay);
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
		if (isManualTestActive())
			return; // manual test overrides normal operation

		if (!isBoilerStarted() && !valvePreheating_) {
			openAllValves();
			return;
		}

		for (size_t valve = 0; valve < valvePorts_.size(); ++valve) {
			bool close = valvesToClose.find(valve) != std::end(valvesToClose);
			handleValve(valve, close);
		}
	}

	void getStatus(std::ostream &ss) const {
		std::lock_guard<std::mutex> lock(mutex_);
		ss << "{\"boiler\": " << (isBoilerStartedUnlocked() ? "true" : "false") << ", \"valvesOpened\": [";
		for (size_t valve = 0; valve < valvesStates_.size(); ++valve) {
			ss << (valvesStates_[valve] ? "true" : "false");
			if (valve + 1 < valvesStates_.size()) {
				ss << ", ";
			}
		}
		ss << "]";
		ss << ", \"valveLabels\": [";
		for (size_t valve = 0; valve < valveLabels_.size(); ++valve) {
			ss << "\"" << valveLabels_[valve] << "\"";
			if (valve + 1 < valveLabels_.size()) {
				ss << ", ";
			}
		}
		ss << "]";
		ss << ", \"supplyReason\": \"" << supplyDecision_.reason << "\", \"outdoorSource\": \"" << supplyDecision_.outdoor.source << "\", \"outdoorQuality\": \"" << supplyDecision_.outdoor.quality << "\"";
		ss << ", \"baseSupplyTemperature\": ";
		if (supplyDecision_.base) ss << static_cast<int>(*supplyDecision_.base); else ss << "null";
		ss << ", \"requestedSupplyTemperature\": ";
		if (supplyDecision_.requested) ss << static_cast<int>(*supplyDecision_.requested); else ss << "null";
		ss << ", \"appliedSupplyTemperature\": ";
		if (currentBoilerState_ && !manualTestActive_ && appliedSupply_.requested) ss << static_cast<int>(*appliedSupply_.requested); else ss << "null";
		ss << ", \"supplyBoostApplied\": " << (currentBoilerState_ && !manualTestActive_ && appliedSupply_.boosted ? "true" : "false");
		if (currentOutdoorTemperature_) {
			ss << ", \"outdoorTemperature\": " << currentOutdoorTemperature_.value();
		}
		if (currentHeatingTemperature_) {
			ss << ", \"heatingTemperature\": " << currentHeatingTemperature_.value();
		}
		if (manualTestActive_) {
			auto remaining = std::chrono::duration_cast<std::chrono::seconds>(manualTestEnd_ - now_()).count();
			if (remaining < 0)
				remaining = 0;
			ss << ", \"manualTest\": {\"active\": true, \"remainingSeconds\": " << remaining << "}";
		}
		ss << "}";
	}

	std::string getStatus() const {

		std::stringstream ss;
		getStatus(ss);
		return ss.str();
	}

	void startManualTest(bool boilerState, std::vector<bool> const &valveStates, uint32_t durationSeconds) {
		DBGLOGFD(log_, logFeature_, "startManualTest boiler: %d, valves: %zu, duration: %ds\n", boilerState, valveStates.size(), durationSeconds);

		{
			std::lock_guard<std::mutex> lock(mutex_);
			manualTestActive_ = true;
			manualTestEnd_ = now_() + std::chrono::seconds(durationSeconds);
			currentBoilerState_ = boilerState;
		}

		boilerPort_->write(boilerState);

		for (size_t i = 0; i < valvePorts_.size() && i < valveStates.size(); ++i) {
			bool opened = valveStates[i];
			handleValve(i, !opened); // handleValve takes "closed" flag
		}
	}

	void stopManualTest() {
		DBGLOGFD(log_, logFeature_, "stopManualTest%s\n", "");
		{
			std::lock_guard<std::mutex> lock(mutex_);
			manualTestActive_ = false;
		}
		// Turn off boiler and open all valves for safety
		boilerPort_->write(false);
		{
			std::lock_guard<std::mutex> lock(mutex_);
			currentBoilerState_ = false;
		}
		openAllValves();
	}

	bool isBoilerStarted() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return isBoilerStartedUnlocked();
	}

	bool isValveOpen(std::string const &label) const {
		std::lock_guard<std::mutex> lock(mutex_);
		for (size_t valve = 0; valve < valveLabels_.size(); ++valve) {
			if (valveLabels_[valve] == label) {
				return valvesStates_[valve];
			}
		}
		return false;
	}

	bool isValveOpen(size_t index) const {
		std::lock_guard<std::mutex> lock(mutex_);
		return index < valvesStates_.size() && valvesStates_[index];
	}

	bool isManualTestActive() {
		if (!manualTestActive_)
			return false;
		if (now_() >= manualTestEnd_) {
			DBGLOGFD(log_, logFeature_, "Manual test expired, stopping%s\n", "");
			stopManualTest();
			return false;
		}
		return true;
	}

private:
	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_{};
	const char *valveLabel(uint8_t nr) const {
		return nr < valveLabels_.size() && !valveLabels_[nr].empty() ? valveLabels_[nr].c_str() : "?";
	}

	void openAllValves() {
		for (size_t valve = 0; valve < valvePorts_.size(); ++valve) {
			handleValve(valve, false);
		}
	}

	void handleValve(uint8_t nr, bool closed) {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			valvesStates_[nr] = !closed;
		}
		valvePorts_[nr]->write(closed); // HIGH = coil on = closed
		// small delay to avoid power surge when multiple valves change at once
#ifdef ARDUINO
		delay(10);
#endif
		DBGLOGFD(log_, logFeature_, "Handle valve %d '%s' state: %s coil %s \n", nr, valveLabel(nr), !closed ? "open " : "close", closed ? "on" : "off");
	}

	void changeBoilerState(bool enabled, boilerHeatingTemperatureOverride_t boilerHeatingTemperatureOverride) {
		std::lock_guard<std::mutex> lock(mutex_);

		supplyDecision_ = resolveSupply(config_, outdoorReading(), boilerHeatingTemperatureOverride);
		currentOutdoorTemperature_ = supplyDecision_.outdoor.temperature;
		const bool ems = config_.boiler.controlMode == config::BoilerConfig::controlMode_t::ems;
		const bool temperatureControl = config_.boiler.controlMode != config::BoilerConfig::controlMode_t::onoff;
		// Legacy installations without an explicit fallback keep the relay usable,
		// but EMS must not manufacture a setpoint when all weather readings disappear.
		if (ems && !supplyDecision_.requested) enabled = false;
		currentBoilerState_ = enabled;
		if (ems) {
			if (supplyDecision_.requested) emsSetHeatingTemperature(config_.boiler.maxHeatingTemp);
			emsChangeBoilerState_(enabled, supplyDecision_.requested.value_or(0));
			if (supplyDecision_.requested) recordAppliedSupply();
		} else {
			if (temperatureControl && supplyDecision_.requested) {
				emsSetHeatingTemperature(*supplyDecision_.requested);
				recordAppliedSupply();
			}
			boilerPort_->write(enabled);
		}
	}

	// A skipped write leaves the previous EMS setting in place, including across
	// relay stops, valve delays and missing-weather periods.
	void recordAppliedSupply() {
		appliedSupply_ = supplyDecision_;
		currentHeatingTemperature_ = *appliedSupply_.requested * 100;
	}

	OutdoorReading outdoorReading() const {
		if (outdoorProvider_) return outdoorProvider_();
		const auto temperature = getOutdoorTemp_();
		return temperature ? OutdoorReading{temperature, "callback", "fresh"} : OutdoorReading{};
	}

	bool isBoilerStartedUnlocked() const { return currentBoilerState_; }

	void emsSetHeatingTemperature(uint8_t temperature) {
		DBGLOGFD(log_, logFeature_, "emsSetHeatingTemperature to: %d\n", temperature);
		emsSetHeatingTemperature_(temperature);
	}


	config::BoilerConfig const &config_;
	getOutdoorTemp_t getOutdoorTemp_;
	std::function<OutdoorReading()> outdoorProvider_;
	SupplyDecision supplyDecision_;
	SupplyDecision appliedSupply_;
	emsChangeBoilerState_t emsChangeBoilerState_;
	emsSetHeatingTemperature_t emsSetHeatingTemperature_;
	std::unique_ptr<gpio::GpioPort> boilerPort_;
	std::vector<std::unique_ptr<gpio::GpioPort>> valvePorts_;
	std::vector<std::string> valveLabels_;
	std::vector<bool> valvesStates_;

	now_t now_;

	bool valvePreheating_ = false;
	clock_t::time_point lastPreheatTime_;

	bool currentBoilerState_ = false; // true - heating
	std::optional<int16_t> currentHeatingTemperature_;
	std::optional<int16_t> currentOutdoorTemperature_;

	bool manualTestActive_ = false;
	clock_t::time_point manualTestEnd_;

	mutable std::mutex mutex_;
};

}
