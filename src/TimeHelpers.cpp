
#include "config.h"

#include <SPI.h>  // for I2C with RTC module
#include <RTClib.h>

#include <cstdio>
#include <cstdlib>


extern "C" {
int setenv(const char *__string, const char *__value, int __overwrite);
}

namespace heating {

class Wrapped_RTC_DS3231 : public RTC_DS3231 {
public:
	bool isStarted() const {
		return started_;
	}

	void start() {
		started_ = true;
	}

private:
	bool started_ = false;
};

Wrapped_RTC_DS3231 rtc;

void setTimeZone(long offset, int daylight) {
	char cst[17] = {0};
	char cdt[17] = "DST";
	char tz[33] = {0};

	if(offset % 3600) {
		sprintf(cst, "UTC%ld:%02lu:%02lu", offset / 3600, std::abs((offset % 3600) / 60), std::abs(offset % 60));
	} else {
		sprintf(cst, "UTC%ld", offset / 3600);
	}
	if(daylight != 3600){
		long tz_dst = offset - daylight;
		if(tz_dst % 3600){
			sprintf(cdt, "DST%ld:%02lu:%02lu", tz_dst / 3600, std::abs((tz_dst % 3600) / 60), std::abs(tz_dst % 60));
		} else {
			sprintf(cdt, "DST%ld", tz_dst / 3600);
		}
	}
	sprintf(tz, "%s%s", cst, cdt);
	setenv("TZ", tz, 1);
}


void setTimeZone(const char *timeZone) {
	setenv("TZ", timeZone, 1);
}

void setLocalTimeFromRTC(std::string const &timeZone, uint32_t ntpUtcOffset, uint32_t ntpDaylightUtcOffset) {
	if (!rtc.isStarted()) {
		heating::logger.printf("setLocalTimeFromRTC: Disabled\n");
		return;
	}

	if (!timeZone.empty()) {
		heating::setTimeZone(timeZone.c_str());
	} else {
		heating::setTimeZone(ntpUtcOffset, ntpDaylightUtcOffset);
	}

	auto dt = heating::rtc.now();
	heating::logger.printf("RTC TIME: %4.4d-%2.2d-%2.2d %2.2d:%2.2d:%2.2d temp: %f\n", dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second(), heating::rtc.getTemperature());

	struct timeval tv = {static_cast<time_t>(dt.unixtime()), 0};
	settimeofday(&tv, nullptr);
}

DateTime getCurrentTime() {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	DateTime dt2(tv.tv_sec);
	heating::logger.printf("getCurrentTime: %4.4d-%2.2d-%2.2d %2.2d:%2.2d:%2.2d\n", dt2.year(), dt2.month(), dt2.day(), dt2.hour(), dt2.minute(), dt2.second());

	struct tm timeinfo;
	getLocalTime(&timeinfo);
	heating::logger.printf("getLocalTime %2.2d:%2.2d:%2.2d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

	return dt2;
}

void setRTCfromLocalTime() {
	if (!rtc.isStarted()) {
		heating::logger.printf("setRTCfromLocalTime: Disabled\n");
		return;
	}

	heating::logger.printf("setRTCfromLocalTime\n");
	heating::rtc.adjust(getCurrentTime());
}

void startRTC(bool rtcEnabled) {
	if (!rtcEnabled) {
		heating::logger.printf("startRTC: Disabled\n");
		return;
	}

	if (heating::rtc.begin()) {
		heating::rtc.disable32K();
		heating::rtc.start();
		auto config = config::getNetworkConfig();
		setLocalTimeFromRTC(config.timeZone, config.ntpUtcOffset, config.ntpDaylightUtcOffset);
	}
}


bool millisDurationPassed(unsigned long now, unsigned long lastJobTime, unsigned long interval) {
	if (now - lastJobTime < interval) {
		return false;
	}
	return true;
}

bool millisDurationPassed(unsigned long lastJobTime, unsigned long interval) {
	auto now = millis();
	return millisDurationPassed(now, lastJobTime, interval);
}

float rtcGetTemp() {
	if (!rtc.isStarted()) {
		return 0.0f;
	}
	return rtc.getTemperature();
}


uint64_t getTimeMillis() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}

}
