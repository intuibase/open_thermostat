#include "UBAParameters.h"
#include "Logger.h"

namespace heating::ems {

void UBAParameters::logData() const {
	{ auto value = getValue<uint8_t>(0); if (value) { DBGLOGEMS("UBAParameters Heating enabled: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(1); if (value) { DBGLOGEMS("UBAParameters Heating temp: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(2); if (value) { DBGLOGEMS("UBAParameters Burn max power: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(3); if (value) { DBGLOGEMS("UBAParameters Burn min power: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(5); if (value) { DBGLOGEMS("UBAParameters Boiler hysteresis On: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(4); if (value) { DBGLOGEMS("UBAParameters Boiler hysteresis Off: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(6); if (value) { DBGLOGEMS("UBAParameters Burn min period: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(7); if (value) { DBGLOGEMS("UBAParameters pump type: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(8); if (value) { DBGLOGEMS("UBAParameters pump delay: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(9); if (value) { DBGLOGEMS("UBAParameters mod max: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(10); if (value) { DBGLOGEMS("UBAParameters mod min: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(11); if (value) { DBGLOGEMS("UBAParameters pump mode: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(12); if (value) { DBGLOGEMS("UBAParameters Boiler2 hysteresis On: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(13); if (value) { DBGLOGEMS("UBAParameters Boiler2 hysteresis Off: %d\n", value.value()); } }
}

}
