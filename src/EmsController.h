#pragma once

#include "config.h"
#include "EmsBusUart.h"
#include "EMS/EmsTelegram.h"
#include "Logger.h"
#include <array>
#include <atomic>
#include <deque>
#include <ostream>
#include <queue>
#include <list>

namespace heating::ems {

struct EmsBoilerState {
	mutable std::mutex mutex;

	// params plus
	std::optional<bool> heatingEnabled;
	//outdoor
	std::optional<int16_t> outdoorTemperature; // for 5.7 EMS telegram returns 57 but we keep in multiplied by 10 = 570
	// wwparamsplus
	std::optional<bool> warmWaterEnabled;
	std::optional<uint8_t> selectedWarmWaterTemperature;
	// monitorfast plus
	std::optional<uint8_t> selectedFlowTemperature;
	std::optional<uint16_t> currentFlowTemperature;
	std::optional<bool> burningGas;
	std::optional<bool> pumpEnabled;
	std::optional<uint8_t> pressure;
	std::optional<uint8_t> currentBurnerPower;
	std::optional<bool> heatingActive;
	std::optional<bool> warmWaterActive;
	std::optional<bool> fillingSiphon;
	std::optional<uint16_t> serviceCode;
	std::optional<std::array<char, 3>> displayCode;
	// slow plus
	std::optional<bool> fanEnabled;
	std::optional<bool> pumpVenting;

	// ww monitor

	std::optional<uint8_t> warmWaterFlow;
	std::optional<uint16_t> currentWarmWaterTemperature;

	std::optional<uint8_t> protocolVersion;

	std::string getStatus() const {
		std::stringstream ss;
		getStatus(ss);
		return ss.str();
	}

	void getStatus(std::ostream &ss) const {
		bool first = true;

		#define ADD_ITEM_TO_JSON(name, value) { if (name.has_value()) { if (!first) { ss << ","; } else { first = false; } ss << "\""#name"\": " << value; } }
		#define ADD_SITEM_TO_JSON(name, value) { if (name.has_value()) { if (!first) { ss << ","; } else { first = false; } ss << "\""#name"\": \"" << value << "\""; } }

		std::lock_guard<std::mutex> lock(mutex);

		ss << "{";
		ADD_ITEM_TO_JSON(warmWaterEnabled, warmWaterEnabled.value())
		ADD_ITEM_TO_JSON(selectedWarmWaterTemperature, static_cast<int>(selectedWarmWaterTemperature.value()))
		ADD_ITEM_TO_JSON(outdoorTemperature, outdoorTemperature.value())
		ADD_ITEM_TO_JSON(heatingEnabled, heatingEnabled.value())
		ADD_ITEM_TO_JSON(selectedFlowTemperature, static_cast<int>(selectedFlowTemperature.value()))
		ADD_ITEM_TO_JSON(currentFlowTemperature, static_cast<int>(currentFlowTemperature.value()))
		ADD_ITEM_TO_JSON(burningGas, burningGas.value())
		ADD_ITEM_TO_JSON(pumpEnabled, pumpEnabled.value())
		ADD_ITEM_TO_JSON(pressure, static_cast<int>(pressure.value()))
		ADD_ITEM_TO_JSON(currentBurnerPower, static_cast<int>(currentBurnerPower.value()))
		ADD_ITEM_TO_JSON(fanEnabled, fanEnabled.value())
		ADD_ITEM_TO_JSON(pumpVenting, pumpVenting.value())
		ADD_ITEM_TO_JSON(fillingSiphon, fillingSiphon.value())
		ADD_ITEM_TO_JSON(warmWaterFlow, static_cast<int>(warmWaterFlow.value()))
		ADD_ITEM_TO_JSON(currentWarmWaterTemperature, currentWarmWaterTemperature.value())
		ADD_ITEM_TO_JSON(heatingActive, heatingActive.value());
		ADD_ITEM_TO_JSON(warmWaterActive, warmWaterActive.value())
		ADD_ITEM_TO_JSON(serviceCode, serviceCode.value())
		ADD_SITEM_TO_JSON(displayCode, displayCode.value().data())

		ss << "}";
	}
};

struct EmsBoilerParams {
	mutable std::mutex mutex;

	std::optional<uint8_t> heatingTemperature;
	std::optional<uint8_t> maximumHeatingTemperature;

	std::optional<uint8_t> getHeatingTemperature() {
		std::lock_guard<std::mutex> lock(mutex);
		return heatingTemperature;
	}

	std::string getJSON() const {
		std::stringstream ss;
		getJSON(ss);
		return ss.str();
	}

	void getJSON(std::ostream &ss) const {
		bool first = true;

		std::lock_guard<std::mutex> lock(mutex);

		ss << "{";
		ADD_ITEM_TO_JSON(maximumHeatingTemperature, static_cast<int>(maximumHeatingTemperature.value()))
		ADD_ITEM_TO_JSON(heatingTemperature, static_cast<int>(heatingTemperature.value()))
		ss << "}";
	}
};


class EmsController {
public:
	static constexpr int maxTxNotConfirmed = 15;
	static constexpr unsigned long boilerParametersReadRequestIntervalSecs = 119;
	static constexpr unsigned long boilerDetailsReadRequestIntervalSecs = 179;
	static constexpr int maxTelegramQueueSize = 59;

	EmsController();

	void requestStartupData();

	void changeBoilerState(bool turnOnHeating, uint8_t heatingTemperature) {
		DBGLOGEMS("changeBoilerState heating: %d temperature: %d\n", turnOnHeating, heatingTemperature);
		if (!emsConfig_.emsEnabled) {
			DBGLOGEMS("bus disabled\n");
			return;
		}

		if (turnOnHeating) {
			startHeating(heatingTemperature);
		} else {
			stopHeating();
		}
	}

	void setHeatingTemperature(uint8_t temperature);


	void loop() {
		if (!emsConfig_.emsEnabled) {
			return;
		}
		processTelegrams();
		requestPeriodicData();

		//TODO log stats periodicly
		// DBGLOGEMS("requestPeriodicData. TxNotConfirmed: %zu\n", txNotConfirmed_.load());

	}

	void registerTelegramProcessor(uint16_t telegramId, std::function<void(EmsTelegram const &)> processor) {
		std::lock_guard<std::mutex> lock(telegramProcessorsMutex_);
		telegramProcessors_[telegramId].push_back(processor);
	}

	EmsBoilerState &getBoilerState() {
		return boilerState_;
	}

	void getStatus(std::ostream &ss) const {
		boilerState_.getStatus(ss);
	}

	std::string getStatus() const {
		return boilerState_.getStatus();
	}

	void getBoilerParams(std::ostream &ss) const {
		boilerParams_.getJSON(ss);
	}

	std::string getBoilerParams() const {
		return boilerParams_.getJSON();
	}

private:
	void requestPeriodicData();

	void reset() {
		DBGLOGFATAL("EmsController::reset\n");
		uart_.reset();
		txNotConfirmed_ = 0;
		{
			std::lock_guard<std::mutex> lock(telegramProcessingMutex_);
			DBGLOGFATAL("EmsController::reset %zu dropped\n", telegramsToSend_.size());
			telegramsToSend_.clear();
		}
		requestStartupData();
	}

	void enqueueTelegramToSend(EmsTelegram telegram, bool front = false) {
		std::unique_lock<std::mutex> lock(telegramProcessingMutex_);

		if (telegramsToSend_.size() == maxTelegramQueueSize) {
			DBGLOGFATAL("EmsController telegramsToSend_.size() = %zu FULL\n", telegramsToSend_.size());
			lock.unlock();
			reset();
			return;
		}

		if (front) {
			telegramsToSend_.push_back(std::move(telegram));
		} else {
			telegramsToSend_.push_front(std::move(telegram));
		}
		DBGLOGEMS("telegramsToSend_.size() = %zu\n", telegramsToSend_.size());
	}

	void processTelegrams();

	void processReadRequest(EmsTelegram const &telegram);

	// called from UART thread - don't block for too long
	void processTelegram(uint8_t *data, uint8_t length);
	bool processPoll(uint8_t deviceId);

	void pong() {
		DBGLOGEMS("pong()\n");
		uint8_t response = deviceId_ | emsMask_.value_or(0);
		uart_.writeToEms(&response, 1);
	}

	void startHeating(uint8_t heatingTemperature);
	void stopHeating();

	config::EmsConfig emsConfig_{config::getEmsConfig()};

	uint8_t boilerId_{0x08};
	// uint8_t deviceId_{0x0B};
	uint8_t deviceId_{0x19}; // TODO get from config/ UI
	std::optional<uint8_t> emsMask_ = {0x80};

	std::mutex telegramProcessingMutex_;
	std::deque<EmsTelegram> telegramsToSend_;
	std::queue<EmsTelegram> telegramsToProcess_;
	std::mutex telegramProcessorsMutex_;
	std::unordered_map<uint16_t, std::list<std::function<void(EmsTelegram const &)>>> telegramProcessors_;

	EmsBoilerState boilerState_;
	EmsBoilerParams boilerParams_;

	std::atomic<std::optional<int16_t>> emsOutdoorTemperature_;

	std::atomic_size_t txNotConfirmed_;

	unsigned long lastPoll_ = 0;

	EmsBusUart uart_{
		[this](uint8_t *data, uint8_t size) { processTelegram(data, size); }
		};


	unsigned long lastBoilerParametersReadRequestMillis_ = 0;
	unsigned long lastBoilerDetailsReadRequestMillis_ = 0;

};


}
