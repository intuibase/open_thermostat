#include "Logging.h"
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

#include <TimeHelpers.h>
#include <esp_timer.h>

namespace heating::ems {

EmsController::EmsController(std::shared_ptr<ib::logger::LoggerInterface> log) : log_(std::move(log)) {
	if (log_) {
		logFeature_ = log_->addFeature("EmsControl");
		verboseFeature_ = log_->addFeature("EmsVerbose");
		fatalFeature_ = log_->addFeature("Fatal");
	}
	DBGLOGFD(log_, logFeature_, "emsEnabled: %d emsForwarderEnabled: %d\n", emsConfig_.emsEnabled, emsConfig_.emsForwarderEnabled);

	if (!emsConfig_.emsEnabled) {
		return;
	}
	config::EmsPins emsPins{config::getEmsPins()};

	captureForeignTelegrams_.store(log_ && log_->isFeatureEnabled(verboseFeature()), std::memory_order_relaxed);
	uart_.start(emsPins.rx, emsPins.tx);

	if (emsConfig_.emsForwarderEnabled) {
		std::optional<config::EmsForwarderPins> emsForwarderPins{config::getEmsForwarderPins()};
		if (emsForwarderPins.has_value()) {
			uart_.startForwarder(emsForwarderPins.value().rx, emsForwarderPins.value().tx);
		}
	}

	#define UPDATE_IF_SET(field, func) { auto _uis__val = telegram->func(); if (_uis__val.has_value()) { field = _uis__val.value();  } }

	registerTelegramProcessor(UBAMonitorFastPlus::predefinedTypeId, [this, &s = boilerState_](EmsTelegram const &t) {
		UBAMonitorFastPlus const *telegram = reinterpret_cast<UBAMonitorFastPlus const *>(&t);
		telegram->logData(*log_, logFeature_);
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

	registerTelegramProcessor(UBAMonitorSlowPlus::predefinedTypeId, [this, &s = boilerState_](EmsTelegram const &t) {
		UBAMonitorSlowPlus const *telegram = reinterpret_cast<UBAMonitorSlowPlus const *>(&t);
		telegram->logData(*log_, logFeature_);
		std::lock_guard<std::mutex> lock(s.mutex);
		UPDATE_IF_SET(s.fanEnabled, getFanEnabled)
	});

	registerTelegramProcessor(UBAMonitorSlowPlus2::predefinedTypeId, [this, &s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAMonitorSlowPlus2 const *>(&t);
		telegram->logData(*log_, logFeature_);
		std::lock_guard<std::mutex> lock(s.mutex);
		UPDATE_IF_SET(s.pumpVenting, getPumpVenting)
	});

	registerTelegramProcessor(UBAParametersWWPlus::predefinedTypeId, [this, &s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAParametersWWPlus const *>(&t);
		telegram->logData(*log_, logFeature_);

		auto enabled = telegram->getWarmWaterEnabled();
		std::lock_guard<std::mutex> lock(s.mutex);
		if (enabled.has_value()) {
			s.warmWaterEnabled = enabled.value();
		}
		UPDATE_IF_SET(s.selectedWarmWaterTemperature, getSelectedWarmWaterTemperature)
	});

	registerTelegramProcessor(UBAParametersPlus::predefinedTypeId, [this, &s = boilerState_, &p = boilerParams_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAParametersPlus const *>(&t);
		telegram->logData(*log_, logFeature_);

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

	registerTelegramProcessor(UBAOutdoorTemp::predefinedTypeId, [this, &s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAOutdoorTemp const *>(&t);
		telegram->logData(*log_, logFeature_);
		std::lock_guard<std::mutex> lock(s.mutex);
		s.outdoorTemperature = telegram->getOutdoorTemperature();
		if (s.outdoorTemperature.has_value()) {
			s.outdoorTemperature.value() *= 10;
		}
	});

	registerTelegramProcessor(UBAMonitorWWPlus::predefinedTypeId, [this, &s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAMonitorWWPlus const *>(&t);
		telegram->logData(*log_, logFeature_);
		std::lock_guard<std::mutex> lock(s.mutex);
		UPDATE_IF_SET(s.warmWaterFlow, getFlow)
		UPDATE_IF_SET(s.currentWarmWaterTemperature, getCurrentTemperature)
	});

	registerTelegramProcessor(UBAProtocolVersion::predefinedTypeId, [this, &s = boilerState_](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAProtocolVersion const *>(&t);
		telegram->logData(*log_, logFeature_);
		std::lock_guard<std::mutex> lock(s.mutex);
		UPDATE_IF_SET(s.protocolVersion, getProtocolVersion)
	});


	registerTelegramProcessor(UBAInternalWeatherCompensatedMode::predefinedTypeId, [this](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAInternalWeatherCompensatedMode const *>(&t);
		telegram->logData(*log_, logFeature_);
	});

	registerTelegramProcessor(UBADeviceVersion::predefinedTypeId, [this](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBADeviceVersion const *>(&t);
		telegram->logData(*log_, logFeature_);
	});

	registerTelegramProcessor(UBAFactory::predefinedTypeId, [this](EmsTelegram const &t) {
		auto telegram = reinterpret_cast<UBAFactory const *>(&t);
		telegram->logData(*log_, logFeature_);
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
	if (lastBoilerParametersReadRequestMillis_ == 0 || ib::millisDurationPassed(now, lastBoilerParametersReadRequestMillis_, 1000ul * boilerParametersReadRequestIntervalSecs)) {
		DBGLOGFD(log_, logFeature_, "requestPeriodicData - outdoor temp and external EMS\n");
		enqueueTelegramToSend(EmsTelegram(EmsTelegram::operation_t::WRITE, deviceId_, 0x08, 0, 0x00E7, {0x00, 0x02, 0x00}), true); // enable external EMS controller
		lastBoilerParametersReadRequestMillis_ = now;
	}

	if (lastBoilerDetailsReadRequestMillis_ == 0 || ib::millisDurationPassed(now, lastBoilerDetailsReadRequestMillis_, 1000ul * boilerDetailsReadRequestIntervalSecs)) {
		DBGLOGFD(log_, logFeature_, "requestPeriodicData - heating and ww params\n");
		enqueueTelegramToSend(UBAParametersWWPlus::getRequest(deviceId_, boilerId_));
		enqueueTelegramToSend(UBAParametersPlus::getRequest(deviceId_, boilerId_));
		enqueueTelegramToSend(UBAOutdoorTemp::getRequest(deviceId_, boilerId_));
		enqueueTelegramToSend(UBAFactory::getRequest(deviceId_, boilerId_));
		// enqueueTelegramToSend(UBAInternalWeatherCompensatedMode::getRequest(deviceId_, boilerId_));
		lastBoilerDetailsReadRequestMillis_ = now;
	}
}

void EmsController::reset() {
	size_t dropped;
	portENTER_CRITICAL(&txMux_);
	dropped = txSize_;
	txHead_ = 0;
	txSize_ = 0;
	portEXIT_CRITICAL(&txMux_);
	txDropped_.fetch_add(dropped, std::memory_order_relaxed);
	// UART recovery is performed by its owner, never concurrently with RX/TX.
	uart_.requestReset();
	DBGLOGFD(log_, fatalFeature(), "EmsController::reset %zu TX dropped\n", dropped);
	requestStartupData();
}

void EmsController::enqueueTelegramToSend(EmsTelegram telegram, bool front) {
	// Encode and allocate before taking the TX lock; poll only copies ready bytes.
	auto encoded = telegram.encodeToRawDataWithCRC();
	if (encoded.empty() || encoded.size() > EmsRawFrame::capacity) {
		DBGLOGFD(log_, fatalFeature(), "Invalid EMS TX size: %zu\n", encoded.size());
		return;
	}
	EmsRawFrame frame;
	frame.length = static_cast<uint8_t>(encoded.size());
	std::copy(encoded.begin(), encoded.end(), frame.data.begin());
	bool full;
	size_t depth;
	portENTER_CRITICAL(&txMux_);
	full = txSize_ == telegramsToSend_.size();
	if (!full) {
		// Preserve the existing ordering: front=true appends; default prepends.
		if (!front) {
			txHead_ = (txHead_ + telegramsToSend_.size() - 1) % telegramsToSend_.size();
		}
		telegramsToSend_[(txHead_ + (front ? txSize_ : 0)) % telegramsToSend_.size()] = frame;
		++txSize_;
	}
	depth = txSize_;
	portEXIT_CRITICAL(&txMux_);
	txHighWater_.observe(depth);
	if (full) {
		txDropped_.fetch_add(1, std::memory_order_relaxed);
		resetRequested_.store(true, std::memory_order_relaxed);
		DBGLOGFD(log_, fatalFeature(), "EMS TX queue full\n");
	} else if (log_) {
		telegram.logDebug(*log_, verboseFeature());
	}
}

bool EmsController::processPoll(uint8_t deviceId) {
	if (deviceId != deviceId_) {
		return false;
	}
	const auto started = esp_timer_get_time();
	polls_.fetch_add(1, std::memory_order_relaxed);
	if (txNotConfirmed_.load(std::memory_order_relaxed) > maxTxNotConfirmed) {
		resetRequested_.store(true, std::memory_order_relaxed);
	}

	EmsRawFrame frame;
	portENTER_CRITICAL(&txMux_);
	if (txSize_ != 0) {
		frame = telegramsToSend_[txHead_];
		txHead_ = (txHead_ + 1) % telegramsToSend_.size();
		--txSize_;
	}
	portEXIT_CRITICAL(&txMux_);
	const uint8_t response = deviceId_ | emsMask_.value_or(0);
	int64_t firstWriteUs = 0;
	const bool written = frame.length == 0
		? uart_.writeToEms(&response, 1, &firstWriteUs)
		: uart_.writeToEms(frame.data.data(), frame.length, &firstWriteUs);
	const auto finished = esp_timer_get_time();
	if (written) {
		if (frame.length == 0) {
			pongs_.fetch_add(1, std::memory_order_relaxed);
		} else {
			txNotConfirmed_.fetch_add(1, std::memory_order_relaxed);
			txFrames_.fetch_add(1, std::memory_order_relaxed);
		}
	} else if (frame.length != 0) {
		txDropped_.fetch_add(1, std::memory_order_relaxed);
	}
	if (firstWriteUs != 0) {
		const auto startDelay = static_cast<uint32_t>(firstWriteUs - started);
		maxPollStartUs_.observe(startDelay);
		const auto threshold = slowPollThresholdUs_.load(std::memory_order_relaxed);
		if (threshold != 0 && startDelay > threshold) {
			slowPolls_.fetch_add(1, std::memory_order_relaxed);
		}
	}
	// Includes intentionally paced transmission, but excludes metric updates.
	maxPollServiceUs_.observe(static_cast<uint32_t>(finished - started));
	return true;
}

void EmsController::processTelegram(uint8_t *data, uint8_t length) {
	// Only the UART task updates confirmation accounting, including recovery.
	const auto resets = uart_.resetCount();
	if (uartResetsSeen_ != resets) {
		uartResetsSeen_ = resets;
		txNotConfirmed_.store(0, std::memory_order_relaxed);
	}
	// UART context: answer polls immediately; copy ordinary frames without waiting.
	if (length == 2 && !(data[0] & 0x80)) {
		processPoll(data[0]);
		return;
	}
	if (length < 7) {
		return;
	}
	// Echo accounting must not depend on loop()/the diagnostic logger keeping up.
	if (data[1] != 0 && (data[1] & 0x7F) != deviceId_) {
		if ((data[0] & 0x7F) == deviceId_ && txNotConfirmed_.load(std::memory_order_relaxed) > 0) {
			txNotConfirmed_.fetch_sub(1, std::memory_order_relaxed);
		}
		// Cache the diagnostic setting in loop(); never call the logger here.
		if (!captureForeignTelegrams_.load(std::memory_order_relaxed)) {
			return;
		}
	}
	if (!telegramsToProcess_.push(data, length)) {
		rxDropped_.fetch_add(1, std::memory_order_relaxed);
	}
}

void EmsController::decodeTelegram(uint8_t *data, uint8_t length) {
	if (log_->isFeatureEnabled(verboseFeature())) {
		DBGLOGFD(log_, verboseFeature(), "processTelegram (%ld): ", length);
		for (size_t i = 0; i < length; ++i) {
			DBGLOGFI(log_, verboseFeature(), "%2.2X ", data[i]);
		}
		DBGLOGFI(log_, verboseFeature(), "\n");
	}

	if (data[1] != 0 && ((data[1] & 0x7F) != deviceId_)) {
		if ((data[0] & 0x7F) == deviceId_) {
			DBGLOGFD(log_, verboseFeature(), "Telegram sent from us. Debug follows:\n");
			// TODO validate sent telegrams by ID?
		} else {
			DBGLOGFD(log_, verboseFeature(), "Telegram not for us. Src: 0x%2.2X Dest: 0x%2.2X, debug follows:\n", data[0] & 0x7F, data[1] & 0x7F);
		}

		if (log_->isFeatureEnabled(verboseFeature())) {
			try {
				EmsTelegram::getFromRawData(*log_, verboseFeature_, data, length - 1).logDebug(*log_, logFeature_);
			} catch (std::exception const &e) {
				decodeErrors_.fetch_add(1, std::memory_order_relaxed);
				DBGLOGFD(log_, fatalFeature(), "processTelegram for debug. Error decoding telegram: %s\n", e.what());
			}
		}
		return;
	}

	if ((data[1] & 0x7F) == deviceId_) {
		DBGLOGFD(log_, verboseFeature(), "We have got an telegram!\n");
	}

	try {
		EmsTelegram telegram = EmsTelegram::getFromRawData(*log_, verboseFeature_, data, length - 1);
		telegram.logDebug(*log_, logFeature_);
		if (telegram.getOperationType() == EmsTelegram::operation_t::READ) {
			processReadRequest(telegram);
		} else {
			std::list<std::function<void(EmsTelegram const &)>> processors;
			{
				std::lock_guard<std::mutex> lock(telegramProcessorsMutex_);
				auto found = telegramProcessors_.find(telegram.getTypeId());
				if (found != telegramProcessors_.end()) {
					processors = found->second;
				}
			}
			for (auto const &processor : processors) {
				processor(telegram);
			}
			if (processors.empty()) {
				DBGLOGFD(log_, logFeature_, "Unknown telegram ID: 0x%4.4X\n", telegram.getTypeId());
			}
		}
	} catch (std::exception const &e) {
		decodeErrors_.fetch_add(1, std::memory_order_relaxed);
		DBGLOGFD(log_, fatalFeature(), "EmsController: error decoding telegram: %s\n", e.what());
	}
}

void EmsController::processTelegrams() {
	EmsRawFrame frame;
	// Bound the work per loop even when the UART producer stays busy.
	for (int i = 0; i < maxTelegramQueueSize && telegramsToProcess_.pop(frame); ++i) {
		decodeTelegram(frame.data.data(), frame.length);
	}
}

EmsControllerStatsSnapshot EmsController::getStatsSnapshot() const {
	return {polls_.load(std::memory_order_relaxed), txFrames_.load(std::memory_order_relaxed),
		pongs_.load(std::memory_order_relaxed), rxDropped_.load(std::memory_order_relaxed),
		txDropped_.load(std::memory_order_relaxed), decodeErrors_.load(std::memory_order_relaxed),
		slowPolls_.load(std::memory_order_relaxed), telegramsToProcess_.highWaterMark(), txHighWater_.load(),
		maxPollStartUs_.load(), maxPollServiceUs_.load(), slowPollThresholdUs_.load(std::memory_order_relaxed)};
}

void EmsController::getDiagnostics(std::ostream &ss) const {
	auto current = getStatsSnapshot();
	// MQTT never resets a measurement window. Use the last completed report.
	current.maxPollStartUs = previousStats_.maxPollStartUs;
	current.maxPollServiceUs = previousStats_.maxPollServiceUs;
	writeEmsDiagnostics(ss, current, uart_.getStatsSnapshot(), txNotConfirmed_.load(std::memory_order_relaxed),
		uart_.forwarderErrors(), uart_.forwarderDropped(), lastStatsMillis_, lastStatsIntervalMs_,
		maxTelegramQueueSize, EmsRawFrame::capacity, EmsBusUartForwarder::PoolCapacity,
		EmsBusUartForwarder::MaxForwarderQueueSize, emsConfig_.emsEnabled);
}

void EmsController::reportStats() {
	const auto now = millis();
	if (!ib::millisDurationPassed(now, lastStatsMillis_, 30000ul)) {
		return;
	}
	const auto intervalMs = static_cast<uint32_t>(now - lastStatsMillis_);
	lastStatsIntervalMs_ = intervalMs;
	lastStatsMillis_ = now;
	auto current = getStatsSnapshot();
	current.maxPollStartUs = maxPollStartUs_.take();
	current.maxPollServiceUs = maxPollServiceUs_.take();
	const auto uart = uart_.getStatsSnapshot();
	const auto forwarderErrors = uart_.forwarderErrors();
	const auto forwarderDropped = uart_.forwarderDropped();
	// Unsigned subtraction also handles one counter wrap between reports.
	auto delta = [](uint32_t value, uint32_t previous) { return static_cast<unsigned long>(value - previous); };
	DBGLOGFD(log_, logFeature_, "EMS interval_ms=%lu poll=%lu tx=%lu pong=%lu echo_pending=%zu rx_drop=%lu tx_drop=%lu decode_err=%lu rx_hwm=%lu tx_hwm=%lu\n",
		static_cast<unsigned long>(intervalMs), delta(current.polls, previousStats_.polls),
		delta(current.txFrames, previousStats_.txFrames), delta(current.pongs, previousStats_.pongs),
		txNotConfirmed_.load(std::memory_order_relaxed), delta(current.rxDropped, previousStats_.rxDropped),
		delta(current.txDropped, previousStats_.txDropped), delta(current.decodeErrors, previousStats_.decodeErrors),
		static_cast<unsigned long>(current.rxHighWater), static_cast<unsigned long>(current.txHighWater));
	DBGLOGFD(log_, logFeature_, "EMS poll_start_max_us=%lu poll_service_max_us=%lu slow_poll=%lu slow_threshold_us=%lu\n",
		static_cast<unsigned long>(current.maxPollStartUs), static_cast<unsigned long>(current.maxPollServiceUs),
		delta(current.slowPolls, previousStats_.slowPolls), static_cast<unsigned long>(current.slowPollThresholdUs));
	DBGLOGFD(log_, logFeature_, "EMS UART rx=%lu bytes=%lu read_err=%lu length_err=%lu fifo_ovf=%lu buffer_full=%lu parity_err=%lu frame_err=%lu timeout=%lu resets=%lu write_err=%lu fwd_err=%lu fwd_drop=%lu\n",
		delta(uart.frames, previousUartStats_.frames), delta(uart.bytes, previousUartStats_.bytes),
		delta(uart.readErrors, previousUartStats_.readErrors), delta(uart.invalidLengths, previousUartStats_.invalidLengths),
		delta(uart.fifoOverflows, previousUartStats_.fifoOverflows), delta(uart.bufferFull, previousUartStats_.bufferFull),
		delta(uart.parityErrors, previousUartStats_.parityErrors), delta(uart.frameErrors, previousUartStats_.frameErrors),
		delta(uart.timeouts, previousUartStats_.timeouts), delta(uart.resets, previousUartStats_.resets),
		delta(uart.writeErrors, previousUartStats_.writeErrors), delta(forwarderErrors, previousForwarderErrors_),
		delta(forwarderDropped, previousForwarderDropped_));
	previousStats_ = current;
	previousUartStats_ = uart;
	previousForwarderErrors_ = forwarderErrors;
	previousForwarderDropped_ = forwarderDropped;
}

void EmsController::processReadRequest(EmsTelegram const &telegram) {
	DBGLOGFD(log_, logFeature_, "processReadRequest, from: 0x%2.2X\n", telegram.getSenderId());

	if (telegram.getTypeId() == UBADeviceVersion::predefinedTypeId) {
		reinterpret_cast<UBADeviceVersion const *>(&telegram)->logData(*log_, logFeature_);
		enqueueTelegramToSend(UBADeviceVersion::getResponse(deviceId_, telegram.getSenderId(), telegram.getOffset(), telegram.getRequestedDataSize()));
	} else { // reply empty message
		DBGLOGFD(log_, logFeature_, "processReadRequest, unknown telegram 0x%4.4X from: 0x%2.2X. Replying with empty msg\n", telegram.getTypeId(), telegram.getSenderId());
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

	DBGLOGFD(log_, logFeature_, "startHeating, temp: %d\n", heatingTemperature);

	enqueueTelegramToSend(EmsTelegram(EmsTelegram::operation_t::WRITE, deviceId_, 0x08, 0, 0x02e0, {0x01, heatingTemperature, 0x64, 0x00, 0x01}), true); // 0x64 - burner power 100%
}

void EmsController::stopHeating() {
	DBGLOGFD(log_, logFeature_, "stopHeating\n");
	enqueueTelegramToSend(EmsTelegram(EmsTelegram::operation_t::WRITE, deviceId_, 0x08, 0, 0x02e0, {0x01, 0x00, 0x00, 0x00, 0x01}), true); // stop heating
}

void EmsController::setHeatingTemperature(uint8_t temperature) {
	auto currentHeatingTemp = boilerParams_.getHeatingTemperature();
	DBGLOGFD(log_, logFeature_, "setHeatingTemperature, temp: %d, current: %d\n", temperature, currentHeatingTemp.value_or(0));

	if (currentHeatingTemp.has_value() && currentHeatingTemp.value() == temperature) {
		DBGLOGFD(log_, logFeature_, "setHeatingTemperature already set, skipping\n");
		return;
	}

	enqueueTelegramToSend(UBAParametersPlus::setHeatingTemperature(deviceId_, boilerId_, temperature));
}

} // namespace heating::ems

//processTelegram (9): 98 08 FF 00 01 EA 00 FA 00
// [EmsControl] (0x98) -W-> (0x8), type: 0x02EA, offset: 0, dataLen: 1 data: 00
