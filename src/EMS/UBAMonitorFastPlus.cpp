
#include "UBAMonitorFastPlus.h"
#include "Logger.h"

namespace heating::ems {

// [EmsControl] (0x88) -B-> (0x0), type: 0x00E4, offset: 0, dataLen: 27 data: 00 2D 2D 00 00 C8 3D 02 6C 64 29 03 00 02 48 00 00 00 00 01 ED 11 00 02 6C 00 00

void UBAMonitorFastPlus::logData() const {
	int8_t dataStart = 0;
	{ auto value = getDisplayCode(); if (value) { DBGLOGEMS("UBAMonitorFastPlus display code %s\n", value.value().data());} }
	{ auto value = getValue<uint16_t>(4); if (value) { DBGLOGEMS("UBAMonitorFastPlus service code %d\n", value.value());} }
	{ auto value = getValue<uint8_t>(6); if (value) { DBGLOGEMS("UBAMonitorFastPlus selFlowTemp %d\n", value.value()); } }
	{ auto value = getValue<uint16_t>(7); if (value) { DBGLOGEMS("UBAMonitorFastPlus curFlowTemp %d\n", value.value());} }
	{ auto value = getValue<uint8_t>(9); if (value) {  DBGLOGEMS("UBAMonitorFastPlus selectedBurnPower %d\n", value.value()); }}
	{ auto value = getValue<uint8_t>(10); if (value) {  DBGLOGEMS("UBAMonitorFastPlus currentBurnPower %d\n", value.value()); }}
	{ auto value = getValue<bool, 0>(11); if (value) {  DBGLOGEMS("UBAMonitorFastPlus burning gas %d\n", value.value()); }}
	{ auto value = getValue<bool, 1>(11); if (value) {  DBGLOGEMS("UBAMonitorFastPlus pump enabled %d\n", value.value()); }}
	{ auto value = getValue<bool, 2>(11); if (value) {  DBGLOGEMS("UBAMonitorFastPlus 3way valve on %d\n", value.value()); }}
	{ auto value = getValue<bool, 6>(11); if (value) {  DBGLOGEMS("UBAMonitorFastPlus siphon filling %d\n", value.value()); }}

	if (getHeatingActive().has_value()) {
		DBGLOGEMS("UBAMonitorFastPlus Heating active: %d\n", getHeatingActive().value());
	}
	if (getWarmWaterActive().has_value()) {
		DBGLOGEMS("UBAMonitorFastPlus Warm Water active: %d\n", getWarmWaterActive().value());
	}

	{ auto value = getValue<bool, 3>(11); if (value) {  DBGLOGEMS("UBAMonitorFastPlus bit3 %d\n", value.value()); }}
	{ auto value = getValue<bool, 4>(11); if (value) {  DBGLOGEMS("UBAMonitorFastPlus bit4 %d\n", value.value()); }}
	{ auto value = getValue<bool, 5>(11); if (value) {  DBGLOGEMS("UBAMonitorFastPlus bit5 %d\n", value.value()); }}
	{ auto value = getValue<bool, 7>(11); if (value) {  DBGLOGEMS("UBAMonitorFastPlus bit7 %d\n", value.value()); }}


	{ auto value = getValue<uint16_t>(17); if (value) {  DBGLOGEMS("UBAMonitorFastPlus return temperature %d\n", value.value()); }}
	{ auto value = getValue<uint16_t>(19); if (value) {  DBGLOGEMS("UBAMonitorFastPlus flame current %d\n", value.value()); }}
	{ auto value = getValue<uint8_t>(21); if (value) {  DBGLOGEMS("UBAMonitorFastPlus pressure %d\n", value.value()); }}
	{ auto value = getValue<int16_t>(23); if (value) {  DBGLOGEMS("UBAMonitorFastPlus current flow temp (my) %d\n", value.value()); }}
	{ auto value = getValue<uint16_t>(31); if (value) {  DBGLOGEMS("UBAMonitorFastPlus exhaust temperature %d\n", value.value()); }}
	{ auto value = getValue<uint8_t>(40); if (value) {  DBGLOGEMS("UBAMonitorFastPlus current burner power %d (relative to max)\n", value.value()); }}
	{ auto value = getValue<uint8_t>(41); if (value) {  DBGLOGEMS("UBAMonitorFastPlus selected burner power %d (relative to max)\n", value.value()); }}
}


}