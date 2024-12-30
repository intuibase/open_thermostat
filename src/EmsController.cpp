#include "EmsController.h"

#include "EMS/UBADeviceVersion.h"
#include "EMS/UBAFactory.h"
#include "EMS/UBAInternalWeatherCompensatedMode.h"
#include "EMS/UBAMonitorFastPlus.h"
#include "EMS/UBAMonitorSlowPlus.h"
#include "EMS/UBAMonitorSlowPlus2.h"
#include "EMS/UBAMonitorWWPlus.h"
#include "EMS/UBAOutdoorTemp.h"
#include "EMS/UBAParametersPlus.h"
#include "EMS/UBAParametersWWPlus.h"
#include "EMS/UBAProtocolVersion.h"

#include "TimeHelpers.h"

namespace heating::ems {

EmsController::EmsController() {
	DBGLOGEMS("emsEnabled: %d emsForwarderEnabled: %d\n", emsConfig_.emsEnabled, emsConfig_.emsForwarderEnabled);

	if (!emsConfig_.emsEnabled) {
		return;
	}
	config::EmsPins emsPins{config::getEmsPins()};

	uart_.start(emsPins.rx, emsPins.tx);

	if (emsConfig_.emsForwarderEnabled) {
		std::optional<config::EmsForwarderPins> emsForwarderPins{config::getEmsForwarderPins()};
		if (emsForwarderPins.has_value()) {
			uart_.startForwarder(emsForwarderPins.value().rx, emsForwarderPins.value().tx);
		}
	}

	#define UPDATE_IF_SET(field, func) { auto _uis__val = telegram->func(); if (_uis__val.has_value()) { field = _uis__val.value();  } }

	registerTelegramProcessor(UBAMonitorFastPlus::predefinedTypeId, [&s = boilerState_](EmsTelegram const &t) {
		UBAMonitorFastPlus const *telegram = reinterpret_cast<UBAMonitorFastPlus const *>(&t);
		telegram->logData();
		std::lock_guard<std::mutex> lock(s.mutex);
		UPDATE_IF_SET(s.selectedFlowTemperature, getSelectedFlowTemperature)
		UPDATE_IF_SET(s.currentFlowTemperature, getCurrentFlowTemperature)
		UPDATE_IF_SET(s.burningGas, getBurningGas)
		UPDATE_IF_SET(s.pumpEnabled, getPumpEnabled)
		UPDATE_IF_SET(s.pressure, getPressure)
		UPDATE_IF_SET(s.currentBurnerPower, getCurrentBurnerPower)
		UPDATE_IF_SET(s.heatingActive, getHeatingActive)
		UPDATE_IF_SET(s.warmWaterActive, getWarmWaterActive)
		UPDATE_IF_SET(s.fillingSiphon, getSiphonFilling)
		UPDATE_IF_SET(s.serviceCode, getServiceCode);
		UPDATE_IF_SET(s.displayCode, getDisplayCode);
	});

	registerTelegramProcessor(UBAMonitorSlowPlus::predefinedTypeId, [&s = boilerState_](EmsTelegram const &t) {
		UBAMonitorSlowPlus const *telegram = reinterpret_cast<UBAMonitorSlowPlus const *>(&t);
		telegram->logData();
		std::lock_guard<std::mutex> lock(s.mutex);
		UPDATE_IF_SET(s.fanEnabled, getFanEnabled)
	});

	registerTelegramProcessor(UBAMonitorSlowPlus2::predefinedTypeId, [&s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAMonitorSlowPlus2 const *>(&t);
		telegram->logData();
		std::lock_guard<std::mutex> lock(s.mutex);
		UPDATE_IF_SET(s.pumpVenting, getPumpVenting)
	});

	registerTelegramProcessor(UBAParametersWWPlus::predefinedTypeId, [&s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAParametersWWPlus const *>(&t);
		telegram->logData();

		auto enabled = telegram->getWarmWaterEnabled();
		std::lock_guard<std::mutex> lock(s.mutex);
		if (enabled.has_value()) {
			s.warmWaterEnabled = enabled.value();
		}
		UPDATE_IF_SET(s.selectedWarmWaterTemperature, getSelectedWarmWaterTemperature)
	});

	registerTelegramProcessor(UBAParametersPlus::predefinedTypeId, [&s = boilerState_, &p = boilerParams_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAParametersPlus const *>(&t);
		telegram->logData();

		{
		std::lock_guard<std::mutex> lock(s.mutex);
		auto enabled = telegram->getHeatingEnabled();
		if (enabled.has_value()) {
			s.heatingEnabled = enabled.value();
		}
		}

		std::lock_guard<std::mutex> lock(p.mutex);
		UPDATE_IF_SET(p.maximumHeatingTemperature, getMaximumHeatingTemperature);
		UPDATE_IF_SET(p.heatingTemperature, getHeatingTemperature);
	});

	registerTelegramProcessor(UBAOutdoorTemp::predefinedTypeId, [&s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAOutdoorTemp const *>(&t);
		telegram->logData();
		std::lock_guard<std::mutex> lock(s.mutex);
		s.outdoorTemperature = telegram->getOutdoorTemperature();
		if (s.outdoorTemperature.has_value()) {
			s.outdoorTemperature.value() *= 10;
		}
	});

	registerTelegramProcessor(UBAMonitorWWPlus::predefinedTypeId, [&s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAMonitorWWPlus const *>(&t);
		telegram->logData();
		std::lock_guard<std::mutex> lock(s.mutex);
		UPDATE_IF_SET(s.warmWaterFlow, getFlow)
		UPDATE_IF_SET(s.currentWarmWaterTemperature, getCurrentTemperature)
	});

	registerTelegramProcessor(UBAProtocolVersion::predefinedTypeId, [&s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAProtocolVersion const *>(&t);
		telegram->logData();
		std::lock_guard<std::mutex> lock(s.mutex);
		UPDATE_IF_SET(s.protocolVersion, getProtocolVersion)
	});


	registerTelegramProcessor(UBAInternalWeatherCompensatedMode::predefinedTypeId, [&s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAInternalWeatherCompensatedMode const *>(&t);
		telegram->logData();
	});

	registerTelegramProcessor(UBADeviceVersion::predefinedTypeId, [&s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBADeviceVersion const *>(&t);
		telegram->logData();
	});

	registerTelegramProcessor(UBAFactory::predefinedTypeId, [](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAMonitorSlowPlus2 const *>(&t);
		telegram->logData();
	});

	requestStartupData();
}

void EmsController::requestStartupData() {
	enqueueTelegramToSend(UBAParametersWWPlus::getRequest(deviceId_, boilerId_));
	enqueueTelegramToSend(UBAParametersPlus::getRequest(deviceId_, boilerId_));
	enqueueTelegramToSend(UBADeviceVersion::getRequest(deviceId_, boilerId_));
	enqueueTelegramToSend(UBAProtocolVersion::getRequest(deviceId_, boilerId_));
	enqueueTelegramToSend(UBAFactory::getRequest(deviceId_, boilerId_));
	enqueueTelegramToSend(EmsTelegram{EmsTelegram::operation_t::READ, deviceId_, boilerId_, 0, 0x04, {EmsTelegram::maxEmsDataLength}}); //ubafactory
	enqueueTelegramToSend(EmsTelegram(EmsTelegram::operation_t::WRITE, deviceId_, 0x08, 0, 0x00E7, {0x00, 0x02, 0x00}), true); // enable external EMS controller
}

void EmsController::requestPeriodicData() {
	if (!emsConfig_.emsEnabled) {
		return;
	}

	auto now = millis();
	if (lastBoilerParametersReadRequestMillis_ == 0 || millisDurationPassed(now, lastBoilerParametersReadRequestMillis_, 1000ul * boilerParametersReadRequestIntervalSecs)) {
		DBGLOGEMS("requestPeriodicData - outdoor temp and external EMS\n");
		enqueueTelegramToSend(EmsTelegram(EmsTelegram::operation_t::WRITE, deviceId_, 0x08, 0, 0x00E7, {0x00, 0x02, 0x00}), true); // enable external EMS controller
		lastBoilerParametersReadRequestMillis_ = now;
	}

	if (lastBoilerDetailsReadRequestMillis_ == 0 || millisDurationPassed(now, lastBoilerDetailsReadRequestMillis_, 1000ul * boilerDetailsReadRequestIntervalSecs)) {
		DBGLOGEMS("requestPeriodicData - heating and ww params\n");
		enqueueTelegramToSend(UBAParametersWWPlus::getRequest(deviceId_, boilerId_));
		enqueueTelegramToSend(UBAParametersPlus::getRequest(deviceId_, boilerId_));
		enqueueTelegramToSend(UBAOutdoorTemp::getRequest(deviceId_, boilerId_));
		enqueueTelegramToSend(UBAFactory::getRequest(deviceId_, boilerId_));
		// enqueueTelegramToSend(UBAInternalWeatherCompensatedMode::getRequest(deviceId_, boilerId_));
		lastBoilerDetailsReadRequestMillis_ = now;
	}
}

bool EmsController::processPoll(uint8_t deviceId) {
	// logger.printf("poll: 0x%X\n", deviceId);

	if (txNotConfirmed_ > maxTxNotConfirmed) {
		DBGLOGFATAL("We have %zu TX not confirmed! Last poll millis: %ld, current millis: %ld\n--- Resetting UART ---\n", txNotConfirmed_.load(), lastPoll_, millis());
		reset();
	}

	if (deviceId != deviceId_) {
		return false;
	}

	lastPoll_ = millis();

	std::unique_lock<std::mutex> lock(telegramProcessingMutex_);
	if (telegramsToSend_.empty()) {
		lock.unlock();
		pong();
	} else {
		DBGLOGEMSVB("poll(), telegrams left in queue: %zu\n", telegramsToSend_.size());

		EmsTelegram telegram = std::move(telegramsToSend_.front());
		telegramsToSend_.pop_front();
		lock.unlock();
		uart_.writeToEms(std::move(telegram.encodeToRawDataWithCRC())); // move is redundant here
		txNotConfirmed_++;
	}
	return true;
}

void EmsController::processTelegram(uint8_t *data, uint8_t length) { // runs on uart thread - log only in verbose mode
	// telegram always ends with BRK - \0

	if (length == 2 && !(data[0] & 0x80)) { // polling if 0 byte is not masked, if masked then it is poll response
		processPoll(data[0]);
		return;
	} else if (length < 7) { // minimum telegram length is 6 bytes + 1 byte BRK
		return;
	}

	if (debug::debug.debugEmsVerbose) {
		DBGLOGEMSVB("processTelegram (%ld): ", length);
		for (size_t i = 0; i < length; ++i) {
			logger.printf("%2.2X ", data[i]);
		}
		logger.printf("\n");
	}

	if (data[1] != 0 && ((data[1] & 0x7F) != deviceId_)) {
		if ((data[0] & 0x7F) == deviceId_) {
			DBGLOGEMSVB("Telegram sent from us. Debug follows:\n");
			if (txNotConfirmed_ > 0) {
				txNotConfirmed_--;
			}
		} else {
			DBGLOGEMSVB("Telegram not for us. Src: 0x%2.2X Dest: 0x%2.2X, debug follows:\n", data[0] & 0x7F, data[1] & 0x7F);
		}

		if (debug::debug.debugEmsVerbose) {
			try {
				EmsTelegram::getFromRawData(data, length - 1);
			} catch (std::exception const &e) {
				DBGLOGFATAL("processTelegram for debug. Error decoding telegram: %s\n", e.what());
			}
		}
		return;
	}

	if ((data[1] & 0x7F) == deviceId_) {
		DBGLOGEMSVB("We have got an telegram!\n");
	}

	try {
		EmsTelegram telegram = EmsTelegram::getFromRawData(data, length - 1);
		std::lock_guard<std::mutex> lock(telegramProcessingMutex_);
		telegramsToProcess_.push(std::move(telegram));
	} catch (std::exception const &e) {
		DBGLOGFATAL("EmsController: error decoding telegram: %s\n", e.what());
	}

}

void EmsController::processTelegrams() {
	std::unique_lock<std::mutex> lock(telegramProcessingMutex_);
	if (telegramsToProcess_.empty()) {
		return;
	} else {
		DBGLOGEMS("EmsController::processTelegrams size: %zu\n", telegramsToProcess_.size());

		do {
			EmsTelegram telegram = std::move(telegramsToProcess_.front());
			telegramsToProcess_.pop();
			lock.unlock();

			if (telegram.getOperationType() == EmsTelegram::operation_t::READ) {
				processReadRequest(telegram);
			} else {
				std::lock_guard<std::mutex> lock(telegramProcessorsMutex_);
				auto processors = telegramProcessors_.find(telegram.getTypeId());
				if (processors != telegramProcessors_.end()) {
					for (auto const &processor : processors->second) {
						processor(telegram);
					}
				} else {
					DBGLOGEMS("Unknown telegram ID: 0x%4.4X\n", telegram.getTypeId());
				}
			}

			lock.lock();
		} while (!telegramsToProcess_.empty());
	}
}

void EmsController::processReadRequest(EmsTelegram const &telegram) {
	DBGLOGEMS("processReadRequest, from: 0x%2.2X\n", telegram.getSenderId());

	if (telegram.getTypeId() == UBADeviceVersion::predefinedTypeId) {
		reinterpret_cast<UBADeviceVersion const *>(&telegram)->logData();
		enqueueTelegramToSend(UBADeviceVersion::getResponse(deviceId_, telegram.getSenderId(), telegram.getOffset(), telegram.getRequestedDataSize()));
	} else { // reply empty message
		DBGLOGEMS("processReadRequest, unknown telegram 0x%4.4X from: 0x%2.2X. Replying with empty msg\n", telegram.getTypeId(), telegram.getSenderId());
		enqueueTelegramToSend(EmsTelegram{EmsTelegram::operation_t::WRITE, deviceId_, telegram.getSenderId(), 0, telegram.getTypeId(), {}});
	}
}

void EmsController::startHeating(uint8_t heatingTemperature) {
	// 1 - always 01
	// 2 - heating temperature
	// 3 - always 0x64 - probably burner power percentage of max burner power
	// 4 - 00 ? heating enabled?
	// 5 - always 01
	// disable heating if:
	// [EmsControl] (0x88) -W-> (0x18), type: 0x02E0, offset: 0, dataLen: 5 data: 01 FF 00 01 01

	DBGLOGEMS("startHeating, temp: %d\n", heatingTemperature);

	enqueueTelegramToSend(EmsTelegram(EmsTelegram::operation_t::WRITE, deviceId_, 0x08, 0, 0x02e0, {0x01, heatingTemperature, 0x64, 0x00, 0x01}), true); // 0x64 - burner power 100%
}

void EmsController::stopHeating() {
	DBGLOGEMS("stopHeating\n");
	enqueueTelegramToSend(EmsTelegram(EmsTelegram::operation_t::WRITE, deviceId_, 0x08, 0, 0x02e0, {0x01, 0x00, 0x00, 0x00, 0x01}), true); // stop heating
}

void EmsController::setHeatingTemperature(uint8_t temperature) {
	auto currentHeatingTemp = boilerParams_.getHeatingTemperature();
	DBGLOGEMS("setHeatingTemperature, temp: %d, current: %d\n", temperature, currentHeatingTemp.value_or(0));

	if (currentHeatingTemp.has_value() && currentHeatingTemp.value() == temperature) {
		DBGLOGEMS("setHeatingTemperature already set, skipping\n");
		return;
	}

	enqueueTelegramToSend(UBAParametersPlus::setHeatingTemperature(deviceId_, boilerId_, temperature));
}

} // namespace heating::ems

//processTelegram (9): 98 08 FF 00 01 EA 00 FA 00
// [EmsControl] (0x98) -W-> (0x8), type: 0x02EA, offset: 0, dataLen: 1 data: 00