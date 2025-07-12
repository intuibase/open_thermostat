#pragma once


#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>

// in case of extending, update REST.h configDebug() too
namespace debug {
	struct debug {
		bool debugRoomTemperatures : 1 = false;
		bool debugAutoLock : 1 = false;
		bool debugHeatingController : 1 = true;
		bool debugTemperatureReader : 1 = true;
		bool debugREST : 1= true;
		bool debugOpenWeather : 1 = false;
		bool debugBoilerController : 1 = true;
		bool debugEmsBusUart : 1= false;
		bool debugEmsBusUartForwarder : 1 = false;
		bool debugEmsVerbose : 1= false;
		bool debugEmsController : 1 = true;
		bool debugMQTT : 1 = true;
		bool debugFatal : 1 = true;
	};
}

namespace debug {
	extern struct debug debug;
}

#define DBGLOGREST(format, ...) if (debug::debug.debugREST) {heating::logger.logf("[%-10.10s] " format, "REST", ##__VA_ARGS__);}
#define DBGLOGEMS(format, ...) if (debug::debug.debugEmsController) {heating::logger.logf("[%-10.10s] " format, "EmsControl", ##__VA_ARGS__);}
#define DBGLOGEMSVB(format, ...) if (debug::debug.debugEmsVerbose) {heating::logger.logf("[%-10.10s] " format, "EmsVerbose", ##__VA_ARGS__);}
#define DBGLOGUART(format, ...) if (debug::debug.debugEmsBusUart) {heating::logger.logf("[%-10.10s] " format, "EmsUart", ##__VA_ARGS__);}
#define DBGLOGUARTFW(format, ...) if (debug::debug.debugEmsBusUartForwarder) {heating::logger.logf("[%-10.10s] " format, "EmsUartFwd", ##__VA_ARGS__);}
#define DBGLOGTR(format, ...) if (debug::debug.debugTemperatureReader) {heating::logger.logf("[%-10.10s] " format, "TempReader", ##__VA_ARGS__);}
#define DBGLOGOW(format, ...) if (debug::debug.debugOpenWeather) {heating::logger.logf("[%-10.10s] " format, "OpenWeathr", ##__VA_ARGS__);}
#define DBGLOGROOM(format, ...) if (debug::debug.debugRoomTemperatures) {heating::logger.logf("[%-10.10s] " format, "Room", ##__VA_ARGS__);}
#define DBGLOGBOILER(format, ...) if (debug::debug.debugBoilerController) {heating::logger.logf("[%-10.10s] " format, "Boiler", ##__VA_ARGS__);}
#define DBGLOGHC(format, ...) if (debug::debug.debugHeatingController) {heating::logger.logf("[%-10.10s] " format, "HeatCtrl", ##__VA_ARGS__);}
#define DBGLOGMQTT(format, ...) if (debug::debug.debugMQTT) {heating::logger.logf("[%-10.10s] " format, "MQTT", ##__VA_ARGS__);}

#define DBGLOGFATAL(format, ...) if (debug::debug.debugFatal) {heating::logger.logf("[%-10.10s] " format, "Fatal", ##__VA_ARGS__);}


#define DBGLOG(...) if (debugLog_) {heating::logger.logf(__VA_ARGS__);}
#define DBGLOGM(__DBGLOGENABLED, module, format, ...) if (__DBGLOGENABLED) {heating::logger.logf("[%-10.10s] " format, module, __VA_ARGS__);}

namespace heating {

class Logger : public WiFiClient {
public:
	void initialize(bool enabled, std::string const &host, uint16_t port) {
		if (!enabled) {
			Serial.println("Network logger not enabled");
			return;
		}

		host_ = host;
		port_ = port;

		if (host_.empty()) {
			return;
		}

		if (!connect(host.c_str(), port, 300)) {
			Serial.println("Network logegr connection to host failed");
		}
		initialized_ = true;
	}

	size_t write(uint8_t data) override {
		reconnect();
		if (initialized_ && connected()) {
			Serial.write(data);
			return WiFiClient::write(data);
		}

		return Serial.write(data);
	}
	size_t write(const uint8_t *buf, size_t size) override {
		reconnect();
		if (initialized_ && connected()) {
			Serial.write(buf, size);
			return WiFiClient::write(buf, size);
		}
		return Serial.write(buf, size);
	}

	size_t logf(const char *format, ...) {
		char loc_buf[200];
		auto timeLen = getTime(loc_buf, 21);

		char * temp = loc_buf;
		va_list arg;
		va_list copy;
		va_start(arg, format);
		va_copy(copy, arg);
		int len = vsnprintf(temp + timeLen, sizeof(loc_buf) - timeLen, format, copy);
		va_end(copy);
		if(len < 0) {
			va_end(arg);
			return 0;
		}
		if(len >= (int)sizeof(loc_buf) - timeLen){  // comparation of same sign type for the compiler
			temp = (char*) malloc(len+1+timeLen);
			if(temp == NULL) {
				va_end(arg);
				return 0;
			}
			memcpy(temp, loc_buf, timeLen);
			len = vsnprintf(temp+timeLen, len+1, format, arg);
		}
		va_end(arg);
		len = write((uint8_t*)temp, len + timeLen);
		if(temp != loc_buf){
			free(temp);
		}
		return len + timeLen;
	}


private:

	size_t getTime(char *buf, size_t size) {
		struct timeval tv;
		gettimeofday(&tv, nullptr);

		time_t nowtime = tv.tv_sec;
		auto timeinfo = localtime(&nowtime);

	    auto len = strftime(buf, size, "%Y-%m-%d %H:%M:%S.", timeinfo);
		sprintf(buf + len, "%3.3ld ", tv.tv_usec/1000);
		return len + 4;
	}

	void reconnect() {
		if (initialized_ && !connected() && millis() > lastReconnect_ + 20000) {
			Serial.printf("Reconnecting logger %s:%d\n", host_.c_str(), port_);
			if (!connect(host_.c_str(), port_, 1000)) {
				Serial.println("Connection to host failed");
			}
			lastReconnect_ = millis();
		}
	}

	bool initialized_ = false;
	uint16_t port_;
	std::string host_;
	unsigned long lastReconnect_ = 0;
};

extern Logger logger;

}
