#include "UBAMonitorWWPlus.h"
#include "Logger.h"

namespace heating::ems {

void UBAMonitorWWPlus::logData() const {
	{ auto value = getValue<uint8_t>(0); if (value) { DBGLOGEMS("UBAMonitorWWPlus set temperature %d\n", value.value()); } }
	{ auto value = getValue<uint16_t>(1); if (value) { DBGLOGEMS("UBAMonitorWWPlus current temperature %d \n", value.value()); } }
	{ auto value = getValue<uint16_t>(3); if (value) { DBGLOGEMS("UBAMonitorWWPlus current temperature2 %d \n", value.value()); } }
	// DBGLOGEMS("POS5: %2.2X\n", getValue<uint8_t>(5).value_or(254));
	// DBGLOGEMS("POS6: %2.2X\n", getValue<uint8_t>(6).value_or(254));
	// DBGLOGEMS("POS7: %2.2X\n", getValue<uint8_t>(7).value_or(254));
	// DBGLOGEMS("POS8: %2.2X\n", getValue<uint8_t>(8).value_or(254));
	{ auto value = getValue<uint8_t>(9); if (value) { DBGLOGEMS("UBAMonitorWWPlus disinfection temp %d \n", value.value()); } }
	// DBGLOGEMS("POS10: %2.2X\n", getValue<uint8_t>(10).value_or(254));

	{ auto value = getValue<uint8_t>(11); if (value) { DBGLOGEMS("UBAMonitorWWPlus ww flow %d l/min\n", value.value()); } }
	// { auto value = getValue<bool, 0>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B12/0 %d\n", value.value()); } }
	// { auto value = getValue<bool, 1>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B12/1 %d\n", value.value()); } }
	{ auto value = getValue<bool, 2>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus one time func on/off %d\n", value.value()); } }
	{ auto value = getValue<bool, 3>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus disinfection on/off %d\n", value.value()); } }
	{ auto value = getValue<bool, 4>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus charging on/off %d\n", value.value()); } }
	// { auto value = getValue<bool, 5>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B12/5 %d\n", value.value()); } }
	// { auto value = getValue<bool, 6>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B12/6 %d\n", value.value()); } }
	// { auto value = getValue<bool, 7>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B12/7 %d\n", value.value()); } }
	// { auto value = getValue<bool, 0>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B13/0 %d\n", value.value()); } }
	// { auto value = getValue<bool, 1>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B13/1 %d\n", value.value()); } }
	{ auto value = getValue<bool, 2>(13); if (value) { DBGLOGEMS("UBAMonitorWWPlus ww circulation on/off %d\n", value.value()); } }
	// { auto value = getValue<bool, 3>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B13/3 %d\n", value.value()); } }
	{ auto value = getValue<bool, 4>(13); if (value) { DBGLOGEMS("UBAMonitorWWPlus recharge on/off %d\n", value.value()); } }
	{ auto value = getValue<bool, 5>(13); if (value) { DBGLOGEMS("UBAMonitorWWPlus temperature ok on/off %d\n", value.value()); } }
	// { auto value = getValue<bool, 6>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B13/6 %d\n", value.value()); } }
	// { auto value = getValue<bool, 7>(12); if (value) { DBGLOGEMS("UBAMonitorWWPlus B13/7 %d\n", value.value()); } }
	{ auto value = getValue<uint32_t>(14); if (value) { DBGLOGEMS("UBAMonitorWWPlus working time %d min \n", value.value()); } }
	{ auto value = getValue<uint32_t>(17); if (value) { DBGLOGEMS("UBAMonitorWWPlus starts %d\n", value.value()); } }
}

}


