#include "UBAFactory.h"
#include "Logger.h"

namespace heating::ems {

void UBAFactory::logData() const {
	{ auto value = getValue<uint8_t>(4); if (value) { DBGLOGEMS("UBAFactory Nominal power: %d kW\n", value.value()); } }
	{ auto value = getValue<uint8_t>(5); if (value) { DBGLOGEMS("UBAFactory Burn min power: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(6); if (value) { DBGLOGEMS("UBAFactory Burn max power: %d\n", value.value()); } }
}

}
