#include "UBAMonitorWWPlus.h"
#include "Logging.h"

namespace heating::ems {

void UBAMonitorWWPlus::logData(ib::logger::LoggerInterface &log, ib::logger::LoggerInterface::LogFeatureType feature) const {
	{ auto value = getValue<uint8_t>(0); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus set temperature %d\n", value.value()); } }
	{ auto value = getValue<uint16_t>(1); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus current temperature %d \n", value.value()); } }
	{ auto value = getValue<uint16_t>(3); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus current temperature2 %d \n", value.value()); } }
	// DBGLOGFD((&log), feature, "POS5: %2.2X\n", getValue<uint8_t>(5).value_or(254));
	// DBGLOGFD((&log), feature, "POS6: %2.2X\n", getValue<uint8_t>(6).value_or(254));
	// DBGLOGFD((&log), feature, "POS7: %2.2X\n", getValue<uint8_t>(7).value_or(254));
	// DBGLOGFD((&log), feature, "POS8: %2.2X\n", getValue<uint8_t>(8).value_or(254));
	{ auto value = getValue<uint8_t>(9); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus disinfection temp %d \n", value.value()); } }
	// DBGLOGFD((&log), feature, "POS10: %2.2X\n", getValue<uint8_t>(10).value_or(254));

	{ auto value = getValue<uint8_t>(11); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus ww flow %d l/min\n", value.value()); } }
	// { auto value = getValue<bool, 0>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B12/0 %d\n", value.value()); } }
	// { auto value = getValue<bool, 1>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B12/1 %d\n", value.value()); } }
	{ auto value = getValue<bool, 2>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus one time func on/off %d\n", value.value()); } }
	{ auto value = getValue<bool, 3>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus disinfection on/off %d\n", value.value()); } }
	{ auto value = getValue<bool, 4>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus charging on/off %d\n", value.value()); } }
	// { auto value = getValue<bool, 5>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B12/5 %d\n", value.value()); } }
	// { auto value = getValue<bool, 6>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B12/6 %d\n", value.value()); } }
	// { auto value = getValue<bool, 7>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B12/7 %d\n", value.value()); } }
	// { auto value = getValue<bool, 0>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B13/0 %d\n", value.value()); } }
	// { auto value = getValue<bool, 1>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B13/1 %d\n", value.value()); } }
	{ auto value = getValue<bool, 2>(13); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus ww circulation on/off %d\n", value.value()); } }
	// { auto value = getValue<bool, 3>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B13/3 %d\n", value.value()); } }
	{ auto value = getValue<bool, 4>(13); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus recharge on/off %d\n", value.value()); } }
	{ auto value = getValue<bool, 5>(13); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus temperature ok on/off %d\n", value.value()); } }
	// { auto value = getValue<bool, 6>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B13/6 %d\n", value.value()); } }
	// { auto value = getValue<bool, 7>(12); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus B13/7 %d\n", value.value()); } }
	{ auto value = getValue<uint32_t>(14); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus working time %d min \n", value.value()); } }
	{ auto value = getValue<uint32_t>(17); if (value) { DBGLOGFD((&log), feature, "UBAMonitorWWPlus starts %d\n", value.value()); } }
}

}


