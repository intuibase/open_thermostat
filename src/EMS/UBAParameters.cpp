#include "UBAParameters.h"
#include "Logging.h"

namespace heating::ems {

void UBAParameters::logData(ib::logger::LoggerInterface &log, ib::logger::LoggerInterface::LogFeatureType feature) const {
	{ auto value = getValue<uint8_t>(0); if (value) { DBGLOGFD((&log), feature, "UBAParameters Heating enabled: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(1); if (value) { DBGLOGFD((&log), feature, "UBAParameters Heating temp: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(2); if (value) { DBGLOGFD((&log), feature, "UBAParameters Burn max power: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(3); if (value) { DBGLOGFD((&log), feature, "UBAParameters Burn min power: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(5); if (value) { DBGLOGFD((&log), feature, "UBAParameters Boiler hysteresis On: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(4); if (value) { DBGLOGFD((&log), feature, "UBAParameters Boiler hysteresis Off: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(6); if (value) { DBGLOGFD((&log), feature, "UBAParameters Burn min period: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(7); if (value) { DBGLOGFD((&log), feature, "UBAParameters pump type: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(8); if (value) { DBGLOGFD((&log), feature, "UBAParameters pump delay: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(9); if (value) { DBGLOGFD((&log), feature, "UBAParameters mod max: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(10); if (value) { DBGLOGFD((&log), feature, "UBAParameters mod min: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(11); if (value) { DBGLOGFD((&log), feature, "UBAParameters pump mode: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(12); if (value) { DBGLOGFD((&log), feature, "UBAParameters Boiler2 hysteresis On: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(13); if (value) { DBGLOGFD((&log), feature, "UBAParameters Boiler2 hysteresis Off: %d\n", value.value()); } }
}

}
