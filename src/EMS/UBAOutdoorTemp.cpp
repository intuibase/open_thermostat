#include "UBAOutdoorTemp.h"
#include "Logger.h"

namespace heating::ems {

void UBAOutdoorTemp::logData() const {
	{ auto value = getValue<int16_t>(0); if (value) { DBGLOGEMS("UBAOutdoorTemp temp: %d\n", value.value()); } } // returns 57 for 5.7
}

}


