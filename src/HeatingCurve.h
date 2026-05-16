#pragma once

#include <array>
#include <cstdint>

namespace heating {

/// Pure heating curve interpolation — no hardware dependencies.
/// @param outdoorTemperature  outdoor temp in 1/100 °C (e.g. 570 = 5.7 °C)
/// @param heatingCurve        9-element array of heating temps in °C for outdoor axis [20,15,10,5,0,-5,-10,-15,-20]
/// @return heating temperature in 1/100 °C
inline int16_t calcHeatingTemperature(int16_t outdoorTemperature, std::array<uint8_t, 9> const &heatingCurve) {
	static constexpr std::array<int8_t, 9> hcOutdoorAxis{20, 15, 10, 5, 0, -5, -10, -15, -20};

	for (size_t idx = 0; idx < hcOutdoorAxis.size(); ++idx) {
		if (hcOutdoorAxis[idx] * 100 <= outdoorTemperature) {
			if (idx == 0) {
				return heatingCurve[idx] * 100;
			} else {
				float slope = static_cast<float>(heatingCurve[idx] - heatingCurve[idx - 1]) /
				              static_cast<float>(hcOutdoorAxis[idx] - hcOutdoorAxis[idx - 1]);
				float intercept = heatingCurve[idx - 1] * 100 - (slope * hcOutdoorAxis[idx - 1] * 100);
				return static_cast<int16_t>(slope * outdoorTemperature + intercept);
			}
		}
	}

	return heatingCurve[8] * 100;
}

}
