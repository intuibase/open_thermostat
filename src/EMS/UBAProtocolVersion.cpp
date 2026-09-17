#include "UBAProtocolVersion.h"
#include "Logging.h"

namespace heating::ems {

void UBAProtocolVersion::logData(ib::logger::LoggerInterface &log, ib::logger::LoggerInterface::LogFeatureType feature) const {
	DBGLOGFD((&log), feature, "ProtocolVersion, offset: %d, size: %d\n", offset_, data_.size());

	if (data_.empty()) {
		return;
	}

	{ auto value = getValue<uint8_t>(0); if (value) { DBGLOGFD((&log), feature, "UBAProtocolVersion: EMS version: %d\n", value.value()); } }
}

}


