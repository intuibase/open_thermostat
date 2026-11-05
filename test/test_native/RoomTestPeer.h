#pragma once
#include "Room.h"
namespace heating {
struct RoomTestPeer {
	static int64_t now() { return std::chrono::duration_cast<std::chrono::seconds>(Room::clock_t::now().time_since_epoch()).count(); }
	static void seedWarmup(Room &r) {
		r.thermalModel_.cancelWarmup();
		r.thermalModel_.observeWarmup(now() - 900, 20, 20, 0, 45, true, true);
		r.thermalModel_.observeWarmup(now() - 600, 20, 21, 0, 45, true, true);
		r.thermalModel_.observeWarmup(now() - 300, 20.2f, 21, 0, 45, true, true);
	}
	static unsigned finishWarmup(Room &r) {
		for (int i = 0; i <= 4; ++i)
			r.thermalModel_.observeWarmup(now() + i * 300, 20.36f + i * .16f, 21, 0, 45, true, true);
		return r.thermalModel_.state.warmupWindows;
	}
	static void forceSample(Room &r) { r.thermalHistory_.reset(); }
	static float predictedSupply(Room &r) { return r.predictedSupply_; }
	static aos::HeatContext context(Room &r) { return r.heatContext_; }
	static bool eligible(Room &r) { return r.warmupEligible_; }
	static bool pendingReset(Room &r) { return r.warmupResetPending_; }
	static bool confirmed(Room &r) { return r.confirmedHeat_; }
	static void seedPrediction(Room &r) { r.predictionCycle_.targetTime = 12345; r.predictionCycle_.clean = true; }
	static bool clean(Room &r) { return r.predictionCycle_.clean; }
	static void dropReading(Room &r) { r.temperatureData_ = {}; }
	static int64_t cacheStamp(Room &r) { return r.lastScheduleCheck_; }
	static void markCache(Room &r) { r.lastScheduleCheck_ = now() - 1; }
	static void setWindows(Room &r) { r.thermalModel_.state.heatingWindows = 12; }
	static unsigned windows(Room &r) { return r.thermalModel_.state.heatingWindows; }
};
}
