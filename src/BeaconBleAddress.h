#pragma once

#include <array>
#include <string>
#include <string_view>
#include <esp_bt_defs.h>

namespace heating {

using BleAddress_t = std::array<uint8_t, ESP_BD_ADDR_LEN>;

#define PRiBleAddress "%02x:%02x:%02x:%02x:%02x:%02x"
#define PRaBleAddress(_addr) _addr[0], _addr[1], _addr[2], _addr[3], _addr[4], _addr[5]

std::string BLEAddressToString(BleAddress_t const &bda);
BleAddress_t BLEAddresFromString(std::string_view address);

}