#pragma once

#include <array>
#include <optional>
#include <vector>
#include <stdint.h>

// quite good ems reference
// https://raw.githubusercontent.com/norberts1/hometop_HT3/master/HT3/docu/HT_EMS_Bus_messages.pdf
// UBA - Universellen Brennerautomaten

namespace heating::ems {

class EmsTelegram {
public:
	enum operation_t {
		READ,
		READ_WITH_DATA,
		WRITE,
		BROADCAST
	};
	static constexpr uint8_t maxEmsDataLength = 27; // EMS1.0

	virtual ~EmsTelegram() {
	}

	EmsTelegram(operation_t operation, uint8_t source, uint8_t destination, uint8_t offset, uint16_t typeId, std::vector<uint8_t> data) :
		EmsTelegram(operation, source, destination, offset, typeId, data.data(), data.size()) {
	}

	EmsTelegram(operation_t operation, uint8_t source, uint8_t destination, uint8_t offset, uint16_t typeId, uint8_t *data, uint8_t dataLength) :
		operation_(operation),
		source_(source),
		destination_(destination),
		offset_(offset),
		typeId_(typeId)
	{
		data_.reserve(dataLength);
		for (uint8_t i = 0; i < dataLength; ++i) {
			data_.push_back(data[i]);
		}
		logDebug();
	}

	uint16_t getTypeId() const;
	uint8_t getSenderId() const;
	uint8_t getOffset() const;
	operation_t getOperationType() const;

	uint8_t getRequestedDataSize() const;

	void logDebug() const;

	static uint16_t getTelegramTypeFromRaw(uint8_t *data, uint8_t length);
	static EmsTelegram getFromRawData(uint8_t *data, uint8_t length); // decodes full raw telegram, data without tailing BRK \0 THROWS

	std::vector<uint8_t> encodeToRawDataWithCRC() const;

	bool operator==(EmsTelegram const &c) {
		return c.operation_ == operation_
		&& c.source_ == source_
		&& c.destination_ == destination_
		&& c.offset_ == offset_
		&& c.typeId_ == typeId_
		&& c.data_ == data_;
	}

protected:

	int8_t getDataOffset(uint8_t dataStart, uint8_t dataLen = 1) const;

	template<typename T, uint8_t bitPos = 0>
	inline std::optional<T> getValue(uint8_t dataStart) const {
		if (offset_ > dataStart || dataStart - offset_ + (std::is_same_v<T, int32_t> ? 3 : sizeof(T)) > data_.size()) {
			return {};
		}
		return getValueAbs<T, bitPos>(dataStart - offset_);
	}

	template<typename T, uint8_t bitPos = 0>
	inline std::optional<T> getValueCustomOffset(uint8_t dataStart, uint8_t offset) const {
		if (offset > dataStart || dataStart - offset + (std::is_same_v<T, int32_t> ? 3 : sizeof(T)) > data_.size()) {
			return {};
		}
		return getValueAbs<T, bitPos>(dataStart - offset);
	}

	template<typename T, uint8_t bitPos = 0>
	inline std::optional<T> getValueAbs(uint8_t dataStart) const {
    	if constexpr (std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t>) {
			return __builtin_bswap16(*reinterpret_cast<const T *>(data_.data() + dataStart));
		} else if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>) {
			return static_cast<T>(data_[dataStart]);
		} else if constexpr (std::is_same_v<T, bool>) {
			return static_cast<T>(data_[dataStart] & (1 << bitPos));
		} else if constexpr (std::is_same_v<T, int32_t>) {
			return static_cast<int32_t>(data_[dataStart] << 16) | static_cast<int32_t>(data_[dataStart + 1] << 8) | data_[dataStart + 2]  | static_cast<int32_t>(static_cast<uint8_t>(data_[dataStart] & 0x80 ? 0xff:00) << 24);
		} else if constexpr (std::is_same_v<T, uint32_t>) {
			return static_cast<int32_t>(data_[dataStart] << 16) | static_cast<int32_t>(data_[dataStart + 1] << 8) | data_[dataStart + 2];
		}
	}

private:
	// CRC lookup table with poly 12 for faster checking - from EMS-ESP32
	static constexpr std::array<uint8_t, 256> ems_crc_table = {0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22, 0x24, 0x26,
								0x28, 0x2A, 0x2C, 0x2E, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x40, 0x42, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4E,
								0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5C, 0x5E, 0x60, 0x62, 0x64, 0x66, 0x68, 0x6A, 0x6C, 0x6E, 0x70, 0x72, 0x74, 0x76,
								0x78, 0x7A, 0x7C, 0x7E, 0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C, 0x8E, 0x90, 0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E,
								0xA0, 0xA2, 0xA4, 0xA6, 0xA8, 0xAA, 0xAC, 0xAE, 0xB0, 0xB2, 0xB4, 0xB6, 0xB8, 0xBA, 0xBC, 0xBE, 0xC0, 0xC2, 0xC4, 0xC6,
								0xC8, 0xCA, 0xCC, 0xCE, 0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC, 0xDE, 0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE,
								0xF0, 0xF2, 0xF4, 0xF6, 0xF8, 0xFA, 0xFC, 0xFE, 0x19, 0x1B, 0x1D, 0x1F, 0x11, 0x13, 0x15, 0x17, 0x09, 0x0B, 0x0D, 0x0F,
								0x01, 0x03, 0x05, 0x07, 0x39, 0x3B, 0x3D, 0x3F, 0x31, 0x33, 0x35, 0x37, 0x29, 0x2B, 0x2D, 0x2F, 0x21, 0x23, 0x25, 0x27,
								0x59, 0x5B, 0x5D, 0x5F, 0x51, 0x53, 0x55, 0x57, 0x49, 0x4B, 0x4D, 0x4F, 0x41, 0x43, 0x45, 0x47, 0x79, 0x7B, 0x7D, 0x7F,
								0x71, 0x73, 0x75, 0x77, 0x69, 0x6B, 0x6D, 0x6F, 0x61, 0x63, 0x65, 0x67, 0x99, 0x9B, 0x9D, 0x9F, 0x91, 0x93, 0x95, 0x97,
								0x89, 0x8B, 0x8D, 0x8F, 0x81, 0x83, 0x85, 0x87, 0xB9, 0xBB, 0xBD, 0xBF, 0xB1, 0xB3, 0xB5, 0xB7, 0xA9, 0xAB, 0xAD, 0xAF,
								0xA1, 0xA3, 0xA5, 0xA7, 0xD9, 0xDB, 0xDD, 0xDF, 0xD1, 0xD3, 0xD5, 0xD7, 0xC9, 0xCB, 0xCD, 0xCF, 0xC1, 0xC3, 0xC5, 0xC7,
								0xF9, 0xFB, 0xFD, 0xFF, 0xF1, 0xF3, 0xF5, 0xF7, 0xE9, 0xEB, 0xED, 0xEF, 0xE1, 0xE3, 0xE5, 0xE7};

	static uint8_t calculateCRC(const uint8_t * data, const uint8_t length); // from EMS-ESP32

protected:
	operation_t operation_;
	uint8_t source_;
	uint8_t destination_;
	uint8_t offset_;
	uint16_t typeId_;
	std::vector<uint8_t> data_;
};

}