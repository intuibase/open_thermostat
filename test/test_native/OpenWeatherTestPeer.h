#pragma once
#include "OpenWeather.h"
namespace heating {
struct OpenWeatherTestPeer {
 static void receive(OpenWeather &weather,const char *json) { weather.parsePayload(String(json)); }
};
}
