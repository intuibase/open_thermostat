#pragma once

#include <string>

namespace heating {

void setRTCfromLocalTime();
void startRTC(bool rtcEnabled);
void setLocalTimeFromRTC(std::string const &timeZone, uint32_t ntpUtcOffset, uint32_t ntpDaylightUtcOffset);
float rtcGetTemp();

} // namespace heating

