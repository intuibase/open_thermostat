#pragma once
#include <cstdint>
#include <cstddef>

class WiFiClient {
public:
	virtual ~WiFiClient() = default;
	virtual size_t write(uint8_t) { return 1; }
	virtual size_t write(const uint8_t* buf, size_t size) { return size; }
	bool connected() { return false; }
	bool connect(const char*, uint16_t, int = 0) { return false; }
};
