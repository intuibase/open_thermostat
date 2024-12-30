#include "UBAParametersPlus.h"
#include "Logger.h"

namespace heating::ems {

void UBAParametersPlus::logData() const {
	// example data: 01 2E 00 52 60 0C 00 01 0A FA 03 01 03 64 01 00 00 00 00 3C 01 01 01 00 01 00
	{ auto value = getValue<uint8_t>(0); if (value) { DBGLOGEMS("UBAParametersPlus Heating enabled: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(1); if (value) { DBGLOGEMS("UBAParametersPlus Heating temp: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(3); if (value) { DBGLOGEMS("UBAParametersPlus Boiler maximum heating temp: %d\n", value.value()); }} //condens 2300i min30-max82
	{ auto value = getValue<uint8_t>(4); if (value) { DBGLOGEMS("UBAParametersPlus Burn max power: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(5); if (value) { DBGLOGEMS("UBAParametersPlus Burn min power: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(8); if (value) { DBGLOGEMS("UBAParametersPlus Boiler hysteresis On: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(9); if (value) { DBGLOGEMS("UBAParametersPlus Boiler hysteresis Off: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(10); if (value) { DBGLOGEMS("UBAParametersPlus Burn min period: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(18); if (value) { DBGLOGEMS("UBAParametersPlus Emergency ops: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(19); if (value) { DBGLOGEMS("UBAParametersPlus Emergency temp: %d\n", value.value()); } }
}

}


