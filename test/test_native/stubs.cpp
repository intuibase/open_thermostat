// Stubs for Arduino-dependent symbols
#include <gtest/gtest.h>
#include "Logger.h"

namespace debug {
struct debug debug;
}

namespace heating {
Logger logger;
}

// RoomConfig method implementations (avoiding Arduino-dependent Logger.h from src/RoomConfig.cpp)
#include "RoomConfig.h"

namespace heating {
bool RoomConfig::TemperatureSetting::doesFit(uint16_t time, uint8_t dayOfTheWeek) const {
	if (time >= 2400)
		return false;
	if (!days_[dayOfTheWeek])
		return false;
	if (timeFrom_ <= timeTo_) {
		return time >= timeFrom_ && time <= timeTo_;
	} else {
		return (time >= timeFrom_) || (time <= timeTo_);
	}
}
bool RoomConfig::TemperatureSetting::isEnabled() const {
	return enabled_;
}
int16_t RoomConfig::TemperatureSetting::getTemperature() const {
	return temperature_;
}
std::optional<uint8_t> RoomConfig::TemperatureSetting::getHeatingTemperatureOverride() const {
	return heatingTemperatureOverride_;
}
} // namespace heating

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
