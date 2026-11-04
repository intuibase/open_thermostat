// LegacyThermalModel.h is generated in a temporary directory for historical replay.
#include "ThermalModel.h"
#include "LegacyThermalModel.h"
#include <iomanip>
#include <iostream>
#include <map>
#include <string>

struct Replay {
	aos_legacy::ThermalHistory history;
	aos_legacy::ThermalModel baseline;
	heating::aos::ThermalHistory candidateHistory;
	heating::aos::ThermalModel candidate;
};

template <class Model> void emit(const Model &model) {
	const auto &s = model.state;
	std::cout << s.gain << ' ' << s.loss << ' ' << s.delayMinutes << ' '
		<< s.fallbackRate << ' ' << s.heatingWindows << ' ' << s.coolingWindows
		<< ' ' << s.fallbackWindows << ' ' << s.warmupRate << ' ' << s.warmupEmission << ' '
		<< s.warmupOutdoorDelta << ' ' << s.warmupWindows << ' ' << s.warmupPhysicalWindows << ' '
		<< model.observation.reason << ' ';
}

int main(int argc, char **argv) {
	if (argc == 2 && std::string(argv[1]) == "--predict") {
		heating::aos::ThermalModel model;
		float room, target, outdoor, supply;
		auto &s = model.state;
		while (std::cin >> s.gain >> s.loss >> s.delayMinutes >> s.fallbackRate
			>> s.heatingWindows >> s.coolingWindows >> s.warmupRate >> s.warmupEmission >> s.warmupOutdoorDelta
			>> s.warmupWindows >> s.warmupPhysicalWindows >> room >> target >> outdoor >> supply) {
			const auto p = model.predict(room, target, outdoor, supply);
			std::cout << p.minutes << ' ' << p.fallback << ' ' << p.unreachable << '\n';
		}
		return std::cin.eof() ? 0 : 3;
	}
	std::map<std::string, Replay> rooms;
	std::string key, room, outdoor, supply, target, curve;
	int64_t time;
	bool heating, valid, warmupValid, cancel, confirmedHeat;
	std::cout << std::setprecision(9);
	while (std::cin >> key >> time >> room >> outdoor >> supply >> heating >> valid >> target >> curve >> warmupValid >> cancel >> confirmedHeat) {
		auto &r = rooms[key];
		if (!r.history.push({time, std::stof(room), std::stof(outdoor), std::stof(supply), heating, valid}) ||
			!r.candidateHistory.push({time, std::stof(room), std::stof(outdoor), std::stof(supply), heating, valid}))
			return 2; // Repeated/out-of-order samples must not silently skew the comparison.
		r.baseline.observe(r.history);
		r.candidate.observe(r.candidateHistory);
		if (cancel) r.candidate.cancelWarmup();
		r.candidate.observeWarmup(time, std::stof(room), std::stof(target), std::stof(outdoor), std::stof(curve), warmupValid, confirmedHeat);
		emit(r.baseline);
		emit(r.candidate);
		std::cout << '\n';
	}
	return std::cin.eof() ? 0 : 3;
}
