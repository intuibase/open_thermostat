#include "UBAParametersWWPlus.h"
#include "Logging.h"

namespace heating::ems {

//                                                                       index 1  2   3  4  5  6 7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26
// [EmsControl] (0x88) -W-> (0x18), type: 0x00EA, offset: 1, dataLen: 26 data: 00 3C 02 00 01 2B 00 00 00 00 00 46 00 00 01 00 00 28 23 3C 64 00 00 00 00 00
//																			dec   60 02                                              35    100
//																			   00 3C 02 00 01 2B 00 00 00 00 00 46 00 00 01 00 00 28 23 3C 64 00 00 00 00 01
// 2 - it might be maximum temperature
// 15 ??
// 21  - 100% ?

void UBAParametersWWPlus::logData(ib::logger::LoggerInterface &log, ib::logger::LoggerInterface::LogFeatureType feature) const {
	{ auto value = getValue<uint8_t>(0); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus sel temp off: %d\n", value.value()); } }

	{ auto value = getValue<uint8_t>(1); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus 1:  %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(2); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus 2:  %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(3); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus 3:  %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(4); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus 4:  %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(15); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus 15: %d\n", value.value()); } }


	{ auto value = getValue<uint8_t>(5); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus enabled: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(6); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus temp set: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(7); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus hysteresis on: %d\n", value.value()); } }
	{ auto value = getValue<int8_t>(8); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus hysteresis off: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(9); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus flow temp offset: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(10); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus circ pump: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(11); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus circ mode: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(12); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus disinfection temp: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(13); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus comfort: %d, 0-comfort, 216-eco - not working check in EMS-ESP\n", value.value()); } }
	{ auto value = getValue<uint8_t>(14); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus alternating operation: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(16); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus selected temp single: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(18); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus selected temp low: %d\n", value.value()); } } // what it means
	{ auto value = getValue<uint8_t>(19); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus minimum temperature: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(20); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus maximum temperature: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(25); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus charge optimization: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(26); if (value) { DBGLOGFD((&log), feature, "UBAParametersWWPlus ECO: %d\n", value.value()); } }
}

}


