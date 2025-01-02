
#include "EMS/UBAMonitorFastPlus.h"
#include "EMS/UBAMonitorSlowPlus.h"
#include "EMS/UBAMonitorWWPlus.h"
#include "EMS/UBAFactory.h"
#include "TimeHelpers.h"

#include <atomic>
#include <iomanip>
#include <mutex>

namespace heating::ems {

class EmsMetrics {
public:
	using registerProcessorFunc_t = std::function<void(uint16_t telegramId, std::function<void(heating::ems::EmsTelegram const &)> processor)>;

	struct BurnerPowerState {
		BurnerPowerState(uint8_t power) : powerPercentage(power), time(getTimeMillis()) {}
		uint8_t powerPercentage = 0;
		uint64_t time = 0;
	};

	struct WarmWaterState {
		WarmWaterState(uint8_t flow) : flow(flow), time(getTimeMillis()) {}
		uint8_t flow = 0; // l/10min  12 means 1.2l/min
		uint64_t time = 0;
	};

	EmsMetrics(registerProcessorFunc_t registerProcessor) {
		registerProcessor(heating::ems::UBAMonitorFastPlus::predefinedTypeId, [this](heating::ems::EmsTelegram const &t) {
			auto telegram = reinterpret_cast<heating::ems::UBAMonitorFastPlus const *>(&t);
			auto power = telegram->getCurrentBurnerPower();
			if (power.has_value()) {
				std::lock_guard<std::mutex> lock(mutex_);
				calculatePowerUsage(BurnerPowerState(*power));
			}

			auto heating = telegram->getHeatingActive();
			if (heating) {
				heatingActive_ = *heating;
			}

			auto warmWater = telegram->getWarmWaterActive();
			if (warmWater) {
				warmWaterActive_ = *warmWater;
			}
		});

		registerProcessor(heating::ems::UBAMonitorWWPlus::predefinedTypeId, [this](heating::ems::EmsTelegram const &t) {
			auto telegram = reinterpret_cast<heating::ems::UBAMonitorWWPlus const *>(&t);

			auto flow = telegram->getFlow();
			if (flow.has_value()) {
				std::lock_guard<std::mutex> lock(mutex_);
				calculateWaterUsage(WarmWaterState(flow.value()));
			}
		});

		// registerProcessor(heating::ems::UBAMonitorSlowPlus::predefinedTypeId, [this](heating::ems::EmsTelegram const &t) {
		// 	// auto telegram = reinterpret_cast<heating::ems::UBAMonitorSlowPlus const *>(&t);
		// });

		registerProcessor(heating::ems::UBAFactory::predefinedTypeId, [this](heating::ems::EmsTelegram const &t) {
			auto telegram = reinterpret_cast<heating::ems::UBAFactory const *>(&t);
			telegram->logData();
			boilerNominalPower_ = telegram->getBoilerNominalPower().value_or(0);
		});
	}

	std::tuple<double, double, double> getGasBurnedKwh() {
		std::lock_guard<std::mutex> lock(mutex_);

		BurnerPowerState currentPowerState(previousPowerState_.powerPercentage);
		calculatePowerUsage(currentPowerState);
		previousPowerState_ = currentPowerState;

		auto energy = std::tuple<double, double, double>(totalEnergyUsedKwh_, heatingEnergyUsedKwh_, warmWaterEnergyUsedKwh_);
		totalEnergyUsedKwh_ = 0;
		heatingEnergyUsedKwh_ = 0;
		warmWaterEnergyUsedKwh_ = 0;
		return energy;
	}

	std::tuple<double, double> getWarmWaterUsage() {
		std::lock_guard<std::mutex> lock(mutex_);

		auto now = getTimeMillis();
		double totalWarmWaterUsage = totalWarmWaterUsage_;
		double flow = totalWarmWaterUsage * 60000 / (now - lastWarmWaterFlowGet_);

		lastWarmWaterFlowGet_ = now;
		totalWarmWaterUsage_ = 0;

		return std::tuple<double, double>(totalWarmWaterUsage, 0);
	}

	// clang-format off
#define ADD_ITEM(name, value) { if (!first) { ss << ","; } else { first = false; } ss << "\""#name"\": " << value;  }
	// clang-format on

	void getMetrics(std::ostream &ss) {
		bool first = true;

		auto [totalEnergyUsedKwh, heatingEnergyUsedKwh, warmWaterEnergyUsedKwh] = getGasBurnedKwh();
		auto [warmWaterUsage, warmWaterAvgFlow] = getWarmWaterUsage();

		ss << "{";
		ADD_ITEM(totalEnergyUsedKwh, totalEnergyUsedKwh);
		ADD_ITEM(warmWaterEnergyUsedKwh, warmWaterEnergyUsedKwh);
		ADD_ITEM(heatingEnergyUsedKwh, heatingEnergyUsedKwh);
		ADD_ITEM(warmWaterUsage, warmWaterUsage);
		ADD_ITEM(warmWaterAvgFlow, warmWaterAvgFlow);
		ss << "}";
	}

#undef ADD_ITEM

private:
	void calculatePowerUsage(BurnerPowerState currentPowerState) {
		auto timeDeltaMs = currentPowerState.time - previousPowerState_.time;
		auto powerUsed = previousPowerState_.powerPercentage;

		double actualPowerKw = static_cast<double>(boilerNominalPower_) * static_cast<double>(previousPowerState_.powerPercentage) / 100.0;
		double energyKwh = (actualPowerKw * timeDeltaMs) / 3600000.0;

		totalEnergyUsedKwh_ += energyKwh;
		if (heatingActive_) {
			heatingEnergyUsedKwh_ += energyKwh;
		}

		if (warmWaterActive_) {
			warmWaterEnergyUsedKwh_ += energyKwh;
		}

		previousPowerState_ = currentPowerState;
	}

	void calculateWaterUsage(WarmWaterState currentWaterState) { //
		auto timeDeltaMs = currentWaterState.time - previousWarmWaterState_.time;

		constexpr double FLOW_SCALING_FACTOR = 10.0;
		constexpr double MILLIS_IN_MIN = 60000.0;

		double flowLm = previousWarmWaterState_.flow / FLOW_SCALING_FACTOR;
		totalWarmWaterUsage_ += (flowLm * timeDeltaMs) / MILLIS_IN_MIN;

		previousWarmWaterState_ = currentWaterState;
	}

private:
	std::mutex mutex_;
	BurnerPowerState previousPowerState_ = {0};
	double totalEnergyUsedKwh_ = 0.0;
	double warmWaterEnergyUsedKwh_ = 0.0;
	double heatingEnergyUsedKwh_ = 0.0;
	bool heatingActive_ = false;
	bool warmWaterActive_ = false;
	std::atomic_uint8_t boilerNominalPower_ = 0;

	WarmWaterState previousWarmWaterState_ = {0};
	double totalWarmWaterUsage_ = 0.0; // liters
	uint64_t lastWarmWaterFlowGet_ = getTimeMillis();
};
} // namespace heating::ems
