#include "UBAParametersPlus.h"
#include "Logging.h"

namespace heating::ems {

void UBAParametersPlus::logData(ib::logger::LoggerInterface &log, ib::logger::LoggerInterface::LogFeatureType feature) const {
	// example data: 01 2E 00 52 60 0C 00 01 0A FA 03 01 03 64 01 00 00 00 00 3C 01 01 01 00 01 00
	{ auto value = getValue<uint8_t>(0); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Heating enabled: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(1); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Heating temp: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(3); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Boiler maximum heating temp: %d\n", value.value()); }} //condens 2300i min30-max82
	{ auto value = getValue<uint8_t>(4); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Burn max power: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(5); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Burn min power: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(8); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Boiler hysteresis On: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(9); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Boiler hysteresis Off: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(10); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Burn min period: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(18); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Emergency ops: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(19); if (value) { DBGLOGFD((&log), feature, "UBAParametersPlus Emergency temp: %d\n", value.value()); } }
}

}


