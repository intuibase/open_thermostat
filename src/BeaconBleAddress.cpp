#include "BeaconBleAddress.h"

namespace heating {

std::string BLEAddressToString(BleAddress_t const &bda) {
	std::string result;
	constexpr size_t size = 18;
	result.reserve(size);
	result.resize(size - 1);
	snprintf(result.data(), size, PRiBleAddress, PRaBleAddress(bda));
	return result;
}

BleAddress_t BLEAddresFromString(std::string_view address) {
	if (address.length() < 17) {
		return BleAddress_t{0, 0, 0, 0, 0, 0};
	}

	// 58:2d:34:3a:71:56
	//           1111111
	// 01234567890123456

	return BleAddress_t{
		static_cast<uint8_t>(std::strtoul(address.data(), nullptr, 16)),
		static_cast<uint8_t>(std::strtoul(address.data() + 3, nullptr, 16)),
		static_cast<uint8_t>(std::strtoul(address.data() + 6, nullptr, 16)),
		static_cast<uint8_t>(std::strtoul(address.data() + 9, nullptr, 16)),
		static_cast<uint8_t>(std::strtoul(address.data() + 12, nullptr, 16)),
		static_cast<uint8_t>(std::strtoul(address.data() + 15, nullptr, 16))
		};
}


}