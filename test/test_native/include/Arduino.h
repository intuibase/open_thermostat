#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdarg>
#include <cstdio>

#define INPUT 0x00
#define OUTPUT 0x03
#define LOW 0x00
#define HIGH 0x01

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline void delay(uint32_t) {}
inline unsigned long millis() { return 0; }

class FakeSerial {
public:
	size_t println(const char*) { return 0; }
	size_t write(uint8_t) { return 1; }
	size_t write(const uint8_t* buf, size_t s) { return s; }
	size_t printf(const char*, ...) { return 0; }
};

inline FakeSerial Serial;

#include <string>
#include <ctime>
class String : public std::string {
public:
 using std::string::string;
 String(const std::string &s) : std::string(s) {}
 bool isEmpty() const { return empty(); }
};
inline int xTaskCreate(void (*)(void *), const char *, uint32_t, void *, int, void *) { return 0; }
inline constexpr int pdPASS = 1;
inline void vTaskDelete(void *) {}
inline unsigned uxTaskGetStackHighWaterMark(void *) { return 0; }
inline bool getLocalTime(tm *t) { const auto now=std::time(nullptr); localtime_r(&now,t); return true; }
struct NativeESP {
 void restart() {}
 unsigned getFreeHeap() { return 0; }
 unsigned getHeapSize() { return 0; }
 unsigned getMinFreeHeap() { return 0; }
 unsigned getMaxAllocHeap() { return 0; }
};
inline NativeESP ESP;
