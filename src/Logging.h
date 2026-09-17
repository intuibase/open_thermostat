#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <logger/LoggerInterface.h>
#include <memory>

namespace heating {
extern std::shared_ptr<ib::logger::LoggerInterface> logger;
}
