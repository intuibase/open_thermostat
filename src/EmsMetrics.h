
#include "EMS/UBAMonitorFastPlus.h"
#include "EMS/UBAMonitorSlowPlus.h"
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
		BurnerPowerState(uint8_t power) : powerPercentage(power), time(getTimeMillis()) {
		}
		uint8_t powerPercentage = 0;
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

		// registerProcessor(heating::ems::UBAMonitorSlowPlus::predefinedTypeId, [this](heating::ems::EmsTelegram const &t) {
		// 	// auto telegram = reinterpret_cast<heating::ems::UBAMonitorSlowPlus const *>(&t);
		// });

		registerProcessor(heating::ems::UBAFactory::predefinedTypeId, [this](heating::ems::EmsTelegram const &t) {
			auto telegram = reinterpret_cast<heating::ems::UBAFactory const *>(&t);
			telegram->logData();
			boilerNominalPower_= telegram->getBoilerNominalPower().value_or(0);
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


	#define ADD_ITEM(name, value) { if (!first) { ss << ","; } else { first = false; } ss << "\""#name"\": " << value;  }

    void getMetrics(std::ostream &ss) {
		bool first = true;

		auto [totalEnergyUsedKwh, heatingEnergyUsedKwh, warmWaterEnergyUsedKwh] = getGasBurnedKwh();

		ss << "{";
		ADD_ITEM(totalEnergyUsedKwh, totalEnergyUsedKwh);
		ADD_ITEM(warmWaterEnergyUsedKwh, warmWaterEnergyUsedKwh);
		ADD_ITEM(heatingEnergyUsedKwh, heatingEnergyUsedKwh);
		ss << "}";
	}

	#undef ADD_ITEM

private:

	void calculatePowerUsage(BurnerPowerState currentPowerState) {
		auto timeDeltaMs = currentPowerState.time - previousPowerState_.time;
		auto powerUsed = previousPowerState_.powerPercentage;

	    uint64_t actualPowerKw = (uint64_t)boilerNominalPower_ * previousPowerState_.powerPercentage / 100;
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

private:
	std::mutex mutex_;
	BurnerPowerState previousPowerState_ = {0};
	double totalEnergyUsedKwh_ = 0.0;
	double warmWaterEnergyUsedKwh_ = 0.0;
	double heatingEnergyUsedKwh_ = 0.0;

	bool heatingActive_ = false;
	bool warmWaterActive_ = false;

	std::atomic_uint8_t boilerNominalPower_ = 0;
};

}
