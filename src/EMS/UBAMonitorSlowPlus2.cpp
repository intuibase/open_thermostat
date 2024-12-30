
#include "UBAMonitorSlowPlus2.h"
#include "Logger.h"

namespace heating::ems {

void UBAMonitorSlowPlus2::logData() const {

	// data[0]: 01 if heating and burning gas
	//			04 if heating and not burning gas (pump active, valve off)
	//			04 if not heating and ww off
	//			04 if warm water
	// data[4]: 04 if heating 00 if warm water, 00 if not heating and no warm water
	// data[5]: 00 if heating
	//          00 if buner off and warm water enabled data[0] == 04
	//          01 if burner on and warm water enabled data[0] == 04

	// data[0] 01 if heating and burning gas else 04
	// data[4] 04 if heating (burning and not burning), else 00
	// data[5] 01 if warm water active else 00
	// data[6] 01 if venting else 00

	{ auto value = getValue<bool, 0>(0); if (value) { DBGLOGEMS("UBAMonitorSlowPlus2 heating active, burning %d\n", value.value()); } } // data[0] == 0x01
	{ auto value = getValue<bool, 2>(4); if (value) { DBGLOGEMS("UBAMonitorSlowPlus2 heating active %d\n", value.value()); } } // data[4]==0x04
	{ auto value = getValue<uint8_t>(5); if (value) { DBGLOGEMS("UBAMonitorSlowPlus2 warm water active %d\n", value.value()); } } // data[5] == 0x01

	{ auto value = getValue<uint8_t>(6); if (value) { DBGLOGEMS("UBAMonitorSlowPlus2 pump venting %d\n", value.value()); } }
	{ auto value = getValue<uint16_t>(11); if (value) { DBGLOGEMS("UBAMonitorSlowPlus2 actual flow temp: %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(13); if (value) { DBGLOGEMS("UBAMonitorSlowPlus2 actual burner power %d %%\n", value.value()); } }
	{ auto value = getValue<uint8_t>(14); if (value) { DBGLOGEMS("UBAMonitorSlowPlus2 maximum burner power?? %d %%\n", value.value()); } }
	{ auto value = getValue<uint8_t>(15); if (value) { DBGLOGEMS("UBAMonitorSlowPlus2 heating temperature %d\n", value.value()); } }
	{ auto value = getValue<uint8_t>(19); if (value) { DBGLOGEMS("UBAMonitorSlowPlus2 warm water temperature set %d\n", value.value()); } } // 0 if disabled
	{ auto value = getValue<uint8_t>(19); if (value && value.value() == 0) { DBGLOGEMS("UBAMonitorSlowPlus2 warm water disabled\n"); } }
}

}