#pragma once

#include "config.h"

namespace heating {

void setTimeZone(const char *timeZone);
void setTimeZone(long offset, int daylight);
void setRTCfromLocalTime();
void startRTC(bool rtcEnabled);
void setLocalTimeFromRTC(std::string const &timeZone, uint32_t ntpUtcOffset, uint32_t ntpDaylightUtcOffset);

bool millisDurationPassed(unsigned long now, unsigned long lastJobTime, unsigned long interval);
bool millisDurationPassed(unsigned long lastJobTime, unsigned long interval);
float rtcGetTemp();

uint64_t getTimeMillis();
}
