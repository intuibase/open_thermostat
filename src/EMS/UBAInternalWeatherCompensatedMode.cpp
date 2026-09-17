#include "UBAInternalWeatherCompensatedMode.h"
#include "Logging.h"

namespace heating::ems {

void UBAInternalWeatherCompensatedMode::logData(ib::logger::LoggerInterface &log, ib::logger::LoggerInterface::LogFeatureType feature) const {
	// [EmsControl] (0x88) -W-> (0x19), type: 0x0028, offset: 0, dataLen: 6 data: 00 5A 14 10 00 05
	//																	dec:		 90 20 16    5
	{ auto value = getValue<bool>(0); if (value) { DBGLOGFD((&log), feature, "UBAInternalWeatherCompensatedMode enabled: %d\n", value.value()); } }
	{ auto value = getValue<bool>(1); if (value) { DBGLOGFD((&log), feature, "UBAInternalWeatherCompensatedMode tempMax: %d\n", value.value()); } }
	{ auto value = getValue<bool>(2); if (value) { DBGLOGFD((&log), feature, "UBAInternalWeatherCompensatedMode tempMin: %d\n", value.value()); } }
	{ auto value = getValue<bool>(3); if (value) { DBGLOGFD((&log), feature, "UBAInternalWeatherCompensatedMode HC ratio 1.6?: %d\n", value.value()); } }
	{ auto value = getValue<bool>(4); if (value) { DBGLOGFD((&log), feature, "UBAInternalWeatherCompensatedMode 4: %d\n", value.value()); } }
	{ auto value = getValue<bool>(5); if (value) { DBGLOGFD((&log), feature, "UBAInternalWeatherCompensatedMode 5: %d\n", value.value()); } }
}

}


