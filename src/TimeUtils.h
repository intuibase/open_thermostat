#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace ib::timeutils {

inline uint16_t parseTimeHHMM(const std::string &str) {
	if (str.length() != 5 || str[2] != ':') {
		throw std::invalid_argument("Invalid time format, expected HH:MM");
	}

	int hours = std::stoi(str.substr(0, 2));
	int minutes = std::stoi(str.substr(3, 2));

	if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
		throw std::out_of_range("Invalid time value");
	}

	return static_cast<uint16_t>(hours * 100 + minutes);
}

}
