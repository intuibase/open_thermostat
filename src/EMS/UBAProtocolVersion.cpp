#include "UBAProtocolVersion.h"
#include "Logger.h"

namespace heating::ems {

void UBAProtocolVersion::logData() const {
	DBGLOGEMS("ProtocolVersion, offset: %d, size: %d\n", offset_, data_.size());

	if (data_.empty()) {
		return;
	}

	{ auto value = getValue<uint8_t>(0); if (value) { DBGLOGEMS("UBAProtocolVersion: EMS version: %d\n", value.value()); } }
}

}


