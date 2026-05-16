
#include "Logger.h"
#include "RoomConfig.h"

namespace heating {

bool RoomConfig::TemperatureSetting::doesFit(uint16_t time, uint8_t dayOfTheWeek) const {
	if (time >= 2400) {
		DBGLOGROOM("doesFit wrong time argument '%d'\n", time);
		return false;
	}

	if (!days_[dayOfTheWeek]) { // not valid (enabled) for day of the week
		DBGLOGROOM("doesFit NO FIT %s time '%d', dayOfTheWeek %d\n", name_.c_str(), time, dayOfTheWeek);
		return false;
	}

	if (timeFrom_ <= timeTo_) {
		// Normal range, same day
		return time >= timeFrom_ && time <= timeTo_;
	} else {
		// Overnight range, e.g. 22:30–06:00
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

}