#include "UBADeviceVersion.h"
#include "Logger.h"

namespace heating::ems {

void UBADeviceVersion::logData() const {
	DBGLOGEMS("Version, offset: %d, size: %d\n", offset_, data_.size());
	// [EmsControl] (0x88) -W-> (0x19), type: 0x0002, offset: 0, dataLen: 12 data: EA 05 06 00 00 00 00 00 00 01 02 68

	if (data_.empty()) {
		return;
	}

	{ auto value = getValue<uint8_t>(9); if (value) { DBGLOGEMS("Version vendorId: %d\n", value.value()); } }

	uint8_t offset = 0;
	if (data_[0] == 0) {
		if (data_.size() >= 3 && data_[3] != 0) {
			offset = 3;
		} else {
			return;
		}
	}

	{ auto value = getValueCustomOffset<uint8_t>(0, offset); if (value) { DBGLOGEMS("Version productId: %d\n", value.value()); } }
	{ auto value = getValueCustomOffset<uint8_t>(1, offset); if (value) { DBGLOGEMS("Version major: %d\n", value.value()); } }
	{ auto value = getValueCustomOffset<uint8_t>(2, offset); if (value) { DBGLOGEMS("Version minor: %d\n", value.value()); } }
}

}