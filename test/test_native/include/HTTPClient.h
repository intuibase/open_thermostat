#pragma once
#include <Arduino.h>
class HTTPClient {
public:
 void begin(const char *) {}
 int GET() { return -1; }
 String getString() { return {}; }
 int getSize() { return 0; }
 void end() {}
};
