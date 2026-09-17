#include "UBAFactory.h"
#include "Logging.h"

namespace heating::ems {

void UBAFactory::logData(ib::logger::LoggerInterface &log, ib::logger::LoggerInterface::LogFeatureType feature) const {
	{ auto value = getValue<uint8_t>(4); if (value) { DBGLOGFD((&log), feature, "UBAFactory Nominal power: %d kW\n", value.value()); } }
	{ auto value = getValue<uint8_t>(5); if (value) { DBGLOGFD((&log), feature, "UBAFactory Burn min power: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(6); if (value) { DBGLOGFD((&log), feature, "UBAFactory Burn max power: %d\n", value.value()); } }
}

}
