#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <optional>

namespace heating::aos {

// Temperatures in Celsius, monotonic seconds. NaN means unavailable.
struct Sample {
	int64_t time = 0;
	float room = NAN, outdoor = NAN, supply = NAN;
	bool heating = false;
	bool valid = false;
};

class ThermalHistory {
public:
	static constexpr size_t capacity = 144;
	bool push(Sample sample) {
		if (size_ && sample.time - newest().time < 300)
			return false;
		data_[next_] = sample;
		next_ = (next_ + 1) % capacity;
		size_ = std::min(size_ + 1, capacity);
		return true;
	}
	const Sample &at(size_t age) const { return data_[(next_ + capacity - 1 - age) % capacity]; }
	const Sample &newest() const { return at(0); }
	size_t size() const { return size_; }

private:
	std::array<Sample, capacity> data_{};
	size_t next_ = 0, size_ = 0;
};

struct ModelState {
	float gain = 3.0f;   // deg C/hour at normalized radiator emission = 1
	float loss = 0.025f; // 1/hour
	float delayMinutes = 10;
	float fallbackRate = 1.0f; // effective ON/OFF deg C/hour
	float meanError = 0, absoluteError = 0, lastError = 0;
	float meanDurationError = 0, durationAbsoluteError = 0, earlyHoldingMinutes = 0;
	uint32_t heatingWindows = 0, coolingWindows = 0, fallbackWindows = 0;
	uint32_t delayCycles = 0, predictions = 0;
	// Whole scheduled warmups include boiler cycling; references use the ordinary curve.
	float warmupRate = 1, warmupEmission = 0, warmupOutdoorDelta = 0;
	uint32_t warmupWindows = 0, warmupPhysicalWindows = 0;
};

inline bool validState(const ModelState &s) {
	if (!std::isfinite(s.warmupRate) || s.warmupRate < 0.05f || s.warmupRate > 6 ||
		!std::isfinite(s.warmupEmission) || s.warmupEmission < 0 || s.warmupEmission > 10 ||
		!std::isfinite(s.warmupOutdoorDelta) || std::abs(s.warmupOutdoorDelta) > 120 ||
		s.warmupPhysicalWindows > s.warmupWindows) return false;
	return std::isfinite(s.gain) && s.gain >= 0.1f && s.gain <= 30 && std::isfinite(s.loss) && s.loss >= 0.001f && s.loss <= 0.5f && std::isfinite(s.delayMinutes) && s.delayMinutes >= 0 && s.delayMinutes <= 60 && std::isfinite(s.fallbackRate) && s.fallbackRate >= 0.1f && s.fallbackRate <= 6 && std::isfinite(s.meanError) && std::abs(s.meanError) <= 1440 && std::isfinite(s.absoluteError) && s.absoluteError >= 0 && s.absoluteError <= 1440 && std::isfinite(s.lastError) && std::abs(s.lastError) <= 1440 && std::isfinite(s.meanDurationError) && std::abs(s.meanDurationError) <= 1440 && std::isfinite(s.durationAbsoluteError) && s.durationAbsoluteError >= 0 && s.durationAbsoluteError <= 1440 && std::isfinite(s.earlyHoldingMinutes) && s.earlyHoldingMinutes >= 0 && s.earlyHoldingMinutes <= 1440;
}

class ThermalModel {
public:
	ModelState state;
	bool dirty = false;
	static float emission(float supply, float room) { return std::pow(std::max(0.0f, supply - room - 5) / 50, 1.3f); }
	bool ready() const { return state.heatingWindows >= 12 && state.coolingWindows >= 6; }
	// Confidence is coverage of 12 heating and 6 cooling windows, not probability.
	unsigned confidence() const { return (std::min(state.heatingWindows, 12u) * 50 / 12) + (std::min(state.coolingWindows, 6u) * 50 / 6); }
	bool calibrated() const { return state.warmupWindows >= 3; }
	void cancelWarmup() { warmupStart_.reset(); previousTarget_.reset(); warmupPreviousTime_.reset(); }
	void observeWarmup(int64_t now, float room, float target, float outdoor, float curveSupply, bool valid, bool delivered) {
		if (!valid || !std::isfinite(room) || !std::isfinite(target) ||
			(warmupPreviousTime_ && (now - *warmupPreviousTime_ > 360 || now <= *warmupPreviousTime_ ||
				std::abs(room - warmupPreviousRoom_) > 0.6f))) {
			cancelWarmup(); return;
		}
		const bool physical = std::isfinite(outdoor) && std::isfinite(curveSupply);
		const float q = physical ? emission(curveSupply, room) : 0;
		const float delta = physical ? room - outdoor : 0;
		if (previousTarget_ && target != *previousTarget_) {
			warmupStart_.reset();
			if (target > *previousTarget_ && target - room >= 0.2f) {
				warmupStart_ = now; warmupStartRoom_ = room; warmupDelay_ = state.delayMinutes;
				warmupQ_ = warmupDelta_ = warmupHeatSeconds_ = 0;
				warmupPhysical_ = physical;
			}
		} else if (warmupStart_ && warmupPreviousTime_) {
			const float seconds = now - *warmupPreviousTime_;
			warmupPhysical_ = warmupPhysical_ && physical;
			warmupQ_ += (warmupPreviousQ_ + q) * 0.5f * seconds;
			warmupDelta_ += (warmupPreviousDelta_ + delta) * 0.5f * seconds;
			if (warmupPreviousDelivered_ && delivered) warmupHeatSeconds_ += seconds;
			const float elapsed = now - *warmupStart_;
			if (room >= target - 0.05f) {
				const float workingMinutes = elapsed / 60 - warmupDelay_;
				const float rate = (room - warmupStartRoom_) * 60 / std::max(1.0f, workingMinutes);
				if (elapsed >= 1800 && elapsed <= 12 * 3600 && workingMinutes >= 15 &&
					warmupHeatSeconds_ >= elapsed * 0.2f && rate >= 0.05f && rate <= 6) {
					++state.warmupWindows;
					const float weight = 1.0f / std::min(state.warmupWindows, 12u);
					state.warmupRate += weight * (rate - state.warmupRate);
					if (warmupPhysical_) {
						++state.warmupPhysicalWindows;
						const float physicalWeight = 1.0f / std::min(state.warmupPhysicalWindows, 12u);
						state.warmupEmission += physicalWeight * (warmupQ_ / elapsed - state.warmupEmission);
						state.warmupOutdoorDelta += physicalWeight * (warmupDelta_ / elapsed - state.warmupOutdoorDelta);
					}
					dirty = true;
				}
				warmupStart_.reset();
			} else if (elapsed > 12 * 3600) warmupStart_.reset();
		}
		previousTarget_ = target; warmupPreviousTime_ = now; warmupPreviousRoom_ = room;
		warmupPreviousQ_ = q; warmupPreviousDelta_ = delta; warmupPreviousDelivered_ = delivered;
	}
	struct Observation {
		const char *reason = "not_sampled";
		bool windowChecked = false, accepted = false, delayUpdated = false;
		float rate = NAN, emission = NAN, roomOutdoorDelta = NAN;
		int64_t windowStart = 0, windowEnd = 0;
	} observation;
	void observe(const ThermalHistory &history) {
		observation = {};
		auto reject = [this](const char *reason) { observation.reason = reason; };
		if (!history.size())
			return reject("empty_history");
		const auto &s = history.newest();
		if (!s.valid || !std::isfinite(s.room)) {
			heatStart_.reset();
			return reject("invalid_sample");
		}
		if (s.heating && (!previousHeating_ || !heatStart_)) {
			heatStart_ = s.time;
			startTemperature_ = s.room;
			delayMeasured_ = false;
		}
		if (!s.heating)
			heatStart_.reset();
		previousHeating_ = s.heating;
		if (heatStart_ && !delayMeasured_ && s.room >= startTemperature_ + 0.1f) {
			const float minutes = (s.time - *heatStart_) / 60.0f;
			if (minutes <= 60) {
				state.delayMinutes += 0.15f * (minutes - state.delayMinutes);
				++state.delayCycles;
				dirty = true;
				observation.delayUpdated = true;
			}
			delayMeasured_ = true;
		}
		// Non-overlapping 30-minute windows: avoid quantized five-minute derivatives.
		if (history.size() < 7)
			return reject("insufficient_history");
		if (lastWindow_ && s.time - *lastWindow_ < 1800)
			return reject("window_not_due");
		lastWindow_ = s.time;
		const auto &first = history.at(6);
		observation.windowChecked = true;
		observation.windowStart = first.time;
		observation.windowEnd = s.time;
		if (s.time - first.time < 1800 || s.time - first.time > 2100)
			return reject("window_duration");
		float q = 0, lossDelta = 0;
		bool physical = true, supplyKnown = true;
		for (size_t i = 0; i <= 6; ++i) {
			const auto &p = history.at(i);
			if (!p.valid || !std::isfinite(p.room))
				return reject("invalid_window_sample");
			if (p.heating != s.heating)
				return reject("mixed_heating_states");
			if (i < 6) {
				const auto &older = history.at(i + 1);
				if (p.time - older.time > 360)
					return reject("sample_gap");
				if (std::abs(p.room - older.room) > 0.6f)
					return reject("temperature_jump");
			}
			supplyKnown = supplyKnown && std::isfinite(p.supply);
			physical = physical && std::isfinite(p.outdoor) && (!s.heating || std::isfinite(p.supply));
			q += s.heating ? emission(p.supply, p.room) : 0;
			lossDelta += p.room - p.outdoor;
		}
		const float rate = (s.room - first.room) * 3600 / (s.time - first.time);
		observation.rate = rate;
		observation.emission = s.heating && !supplyKnown ? NAN : q / 7;
		observation.roomOutdoorDelta = lossDelta / 7;
		if (std::abs(rate) > 4)
			return reject("implausible_rate");
		if (s.heating) {
			if (!heatStart_ || first.time < *heatStart_ + state.delayMinutes * 60)
				return reject("response_delay");
			// Valid delivered heat can merely offset losses. Ignoring flat/slow
			// windows selects only successful warming and overestimates capacity.
			// Keep delay, anomaly, emission and parameter bounds for these windows.
			state.fallbackRate = std::clamp(state.fallbackRate + 0.15f * (rate - state.fallbackRate), 0.1f, 6.0f);
			++state.fallbackWindows;
			q /= 7;
			lossDelta /= 7;
			observation.reason = !physical ? "fallback_missing_inputs" : "fallback_low_emission";
			if (physical && q > 0.03f) {
				const float gain = (rate + state.loss * lossDelta) / q;
				observation.reason = "fallback_gain_out_of_range";
				if (gain >= 0.1f && gain <= 30) {
					state.gain += 0.15f * (gain - state.gain);
					++state.heatingWindows;
					observation.reason = "heating_accepted";
				}
			}
			dirty = true;
			observation.accepted = true;
		} else {
			if (!physical)
				return reject("cooling_missing_outdoor");
			if (lossDelta / 7 <= 3)
				return reject("insufficient_outdoor_delta");
			if (rate > -0.05f)
				return reject("insufficient_cooling");
			const float loss = -rate / (lossDelta / 7);
			if (loss < 0.001f || loss > 0.5f)
				return reject("loss_out_of_range");
			state.loss += 0.15f * (loss - state.loss);
			++state.coolingWindows;
			dirty = true;
			observation.reason = "cooling_accepted";
			observation.accepted = true;
		}
	}

	struct Prediction {
		int minutes = 0;
		bool fallback = true;
		bool unreachable = false;
		bool calibrated = false;
	};
	Prediction predict(float room, float target, float outdoor, float supply) const {
		if (!std::isfinite(room) || !std::isfinite(target))
			return {0, true, true};
		if (room >= target)
			return {};
		const bool calibrated = this->calibrated();
		const bool physical = (calibrated ? state.warmupPhysicalWindows == state.warmupWindows : ready()) &&
			std::isfinite(outdoor) && std::isfinite(supply);
		if (!physical) {
			return {static_cast<int>(std::ceil((target - room) / (calibrated ? state.warmupRate : state.fallbackRate) * 60 + state.delayMinutes)), true, false, calibrated};
		}
		float temperature = room;
		for (int minute = 1; minute <= 1440; ++minute) {
			const float rate = calibrated ? state.warmupRate + state.gain * (emission(supply, temperature) - state.warmupEmission) -
				state.loss * (temperature - outdoor - state.warmupOutdoorDelta) :
				state.gain * emission(supply, temperature) - state.loss * (temperature - outdoor);
			if (rate <= 0.001f)
				return {1440, false, true, calibrated};
			temperature += rate / 60;
			if (temperature >= target)
				return {minute + static_cast<int>(std::ceil(state.delayMinutes)), false, false, calibrated};
		}
		return {1440, false, true, calibrated};
	}
	void recordError(float minutes, float durationError = 0) {
		if (!std::isfinite(minutes) || std::abs(minutes) > 1440 || !std::isfinite(durationError) || std::abs(durationError) > 1440)
			return;
		++state.predictions;
		const float weight = 1.0f / std::min(state.predictions, 100u);
		state.lastError = minutes;
		state.meanError += weight * (minutes - state.meanError);
		state.absoluteError += weight * (std::abs(minutes) - state.absoluteError);
		state.meanDurationError += weight * (durationError - state.meanDurationError);
		state.durationAbsoluteError += weight * (std::abs(durationError) - state.durationAbsoluteError);
		state.earlyHoldingMinutes += weight * (std::max(0.0f, -minutes) - state.earlyHoldingMinutes);
		dirty = true;
	}

private:
	std::optional<int64_t> warmupStart_, warmupPreviousTime_;
	std::optional<float> previousTarget_;
	float warmupStartRoom_ = 0, warmupDelay_ = 0, warmupQ_ = 0, warmupDelta_ = 0, warmupHeatSeconds_ = 0;
	float warmupPreviousRoom_ = 0, warmupPreviousQ_ = 0, warmupPreviousDelta_ = 0;
	bool warmupPhysical_ = false, warmupPreviousDelivered_ = false;
	std::optional<int64_t> lastWindow_, heatStart_;
	bool previousHeating_ = false, delayMeasured_ = false;
	float startTemperature_ = 0;
};
} // namespace heating::aos
