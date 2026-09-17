
#include "UBAMonitorSlowPlus.h"
#include "Logging.h"

namespace heating::ems {

void UBAMonitorSlowPlus::logData(ib::logger::LoggerInterface &log, ib::logger::LoggerInterface::LogFeatureType feature) const {
	{ auto value = getValue<bool, 0>(2); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus B2/0 (burnGas?) %d\n", value.value()); } }
	{ auto value = getValue<bool, 1>(2); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus B2/1 %d\n", value.value()); } }
	{ auto value = getValue<bool, 2>(2); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus fan working %d\n", value.value()); } }
	{ auto value = getValue<bool, 3>(2); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus ignition %d\n", value.value()); } }
	{ auto value = getValue<bool, 4>(2); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus B2/4 %d\n", value.value()); } }
	{ auto value = getValue<bool, 5>(2); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus pump enabled %d\n", value.value()); } }
	{ auto value = getValue<bool, 6>(2); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus B2/6 %d\n", value.value()); } }
	{ auto value = getValue<bool, 7>(2); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus ww circ on %d\n", value.value()); } }
	{ auto value = getValue<uint16_t>(6); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus exhaust temperature %d \n", value.value()); } } // na pewno jak ogrzewa dom
	{ auto value = getValue<uint8_t>(25); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus heating pump modulation %d %%\n", value.value()); } }
	{ auto value = getValue<int32_t>(10); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus burn starts %d\n", value.value()); } }
	{ auto value = getValue<int32_t>(13); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus burn working %d min\n", value.value()); } }
	{ auto value = getValue<int32_t>(16); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus burn 2 working %d min\n", value.value()); } }
	{ auto value = getValue<int32_t>(19); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus heating working %d min\n", value.value()); } }
	{ auto value = getValue<int32_t>(22); if (value) { DBGLOGFD((&log), feature, "UBAMonitorSlowPlus heating starts %d\n", value.value()); } }
}

}