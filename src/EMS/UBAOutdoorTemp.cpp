#include "UBAOutdoorTemp.h"
#include "Logging.h"

namespace heating::ems {

void UBAOutdoorTemp::logData(ib::logger::LoggerInterface &log, ib::logger::LoggerInterface::LogFeatureType feature) const {
	{ auto value = getValue<int16_t>(0); if (value) { DBGLOGFD((&log), feature, "UBAOutdoorTemp temp: %d\n", value.value()); } } // returns 57 for 5.7
}

}


