#pragma once
#include "BeaconTemperatureReader.h"
#include "OpenWeather.h"
#include "EmsController.h"
#include "EmsMetrics.h"
#include "MQTT.h"
#include "HeatingControllerCore.h"

namespace heating {
struct HeatingControllerServices {
	using BeaconTemperatureReader = heating::BeaconTemperatureReader;
	using OpenWeather = heating::OpenWeather;
	using EmsController = ems::EmsController;
	using EmsMetrics = ems::EmsMetrics;
	using MQTT = heating::MQTT;
};
using HeatingController = HeatingControllerCore<HeatingControllerServices>;
}
