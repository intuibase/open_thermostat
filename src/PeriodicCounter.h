#pragma once

#include <algorithm>
#include <cstdint>
#include <esp_timer.h>

namespace ib {

class PeriodicCounter {
public:
	PeriodicCounter(uint64_t intervalMs) : intervalMs_(std::min(intervalMs, UINT64_MAX / 1000)) {}

	bool durationPassed() {
		int64_t now = esp_timer_get_time();

		if (lastPassed_ != 0) {
			int64_t elapsed = now - lastPassed_;
			if (elapsed >= 0 && static_cast<uint64_t>(elapsed) < (intervalMs_ * 1000)) {
				return false;
			}
		}

		lastPassed_ = now;
		return true;
	}

	int64_t getTimeToWaitMs() {
		int64_t now = esp_timer_get_time();
		if (lastPassed_ == 0) {
			return intervalMs_;
		}
		int64_t elapsed = now - lastPassed_;

		if (elapsed >= (intervalMs_ * 1000)) {
			return 0;
		}
		return (intervalMs_ * 1000 - elapsed) / 1000;
	}

	uint64_t getIntervalMs() { return intervalMs_; }

	void notifyNow() { lastPassed_ = esp_timer_get_time(); }

private:
	int64_t lastPassed_ = 0;
	uint64_t intervalMs_;
};

} // namespace ib