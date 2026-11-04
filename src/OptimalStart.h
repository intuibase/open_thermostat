#pragma once
#include "ThermalModel.h"
#include <ctime>

namespace heating::aos {
struct Config {
	bool thermalLearningEnabled = false;
	bool optimalStartEnabled = false;
	bool shadowMode = true;
	uint16_t maxLeadMinutes = 240;
	uint16_t marginMinutes = 10;
};

struct Event {
	int64_t targetTime = 0, previousTime = 0;
	int16_t temperature = 0; // hundredths of a degree
};

// Resolve effective schedule transitions, including overlapping ranges, base
// temperature, weekdays, inclusive end minutes, and DST via caller's resolver.
template <class Resolve>
Event nextIncrease(int64_t now, Resolve resolve) {
	const int64_t minute = now - now % 60;
	auto current = resolve(minute);
	int64_t previous = minute - 12 * 3600;
	for (int i = 0; i < 720; ++i) {
		const auto t = minute - i * 60;
		if (resolve(t - 60) != resolve(t)) {
			previous = t;
			break;
		}
	}
	for (int i = 1; i <= 720; ++i) {
		const auto t = minute + i * 60;
		const auto next = resolve(t);
		if (next > current)
			return {t, previous, next};
		if (next != current)
			previous = t;
		current = next;
	}
	return {};
}

class OptimalStartScheduler {
public:
	enum class Status { OFF, NO_DATA, NO_EVENT, OVERRIDE, WAITING, PREHEATING, SHADOW };
	struct Decision {
		Status status = Status::OFF;
		Event event;
		ThermalModel::Prediction prediction;
		int64_t startTime = 0;
		bool limited = false;
		bool apply = false;
	} decision;
	void reset() {
		latchedTime_ = 0;
		decision = {};
	}
	void update(const Config &config, int64_t now, Event event, const ThermalModel &model, float room, float outdoor, float supply, bool overrideActive, bool valid) {
		decision = {};
		decision.event = event;
		if (!config.optimalStartEnabled) {
			reset();
			return;
		}
		if (!valid || !std::isfinite(room)) {
			latchedTime_ = 0;
			decision.status = Status::NO_DATA;
			return;
		}
		if (overrideActive) {
			latchedTime_ = 0;
			decision.status = Status::OVERRIDE;
			return;
		}
		if (!event.targetTime || event.targetTime <= now) {
			latchedTime_ = 0;
			decision.status = Status::NO_EVENT;
			return;
		}
		if (latchedTime_ != event.targetTime)
			latchedTime_ = 0;
		decision.prediction = model.predict(room, event.temperature / 100.0f, outdoor, supply);
		const auto lead = decision.prediction.minutes + config.marginMinutes;
		const auto earliest = std::max(event.previousTime, event.targetTime - static_cast<int64_t>(std::clamp<int>(config.maxLeadMinutes, 60, 720)) * 60);
		decision.startTime = std::max(earliest, event.targetTime - static_cast<int64_t>(lead) * 60);
		decision.limited = lead * 60 > event.targetTime - earliest || decision.prediction.unreachable;
		if (latchedTime_ || (now >= decision.startTime && room < event.temperature / 100.0f)) {
			latchedTime_ = event.targetTime;
			decision.status = config.shadowMode ? Status::SHADOW : Status::PREHEATING;
			decision.apply = !config.shadowMode;
		} else
			decision.status = Status::WAITING;
	}
	static const char *name(Status status) {
		switch (status) {
			case Status::OFF:
				return "DISABLED";
			case Status::NO_DATA:
				return "NO_DATA";
			case Status::NO_EVENT:
				return "NO_EVENT";
			case Status::OVERRIDE:
				return "OVERRIDE";
			case Status::WAITING:
				return "WAITING";
			case Status::PREHEATING:
				return "PREHEATING";
			case Status::SHADOW:
				return "SHADOW";
		}
		return "DISABLED";
	}

private:
	int64_t latchedTime_ = 0;
};
} // namespace heating::aos
