#include "EmsTelegram.h"

#include "Logger.h"

namespace heating::ems {

uint16_t EmsTelegram::getTypeId() const {
	return typeId_;
}

uint8_t EmsTelegram::getSenderId() const {
	return source_ & 0x7F; // strip MSB
}

uint8_t EmsTelegram::getOffset() const {
	return offset_;
}

EmsTelegram::operation_t EmsTelegram::getOperationType() const {
	return operation_;
}

uint8_t EmsTelegram::getRequestedDataSize() const {
	if (data_.empty()) {
		return 0;
	}
	return data_[0];
}

void EmsTelegram::logDebug() const {
	if (debug::debug.debugEmsController) {
		char opType = 'B';
		if (operation_ == operation_t::READ) {
			opType = 'R';
		} else if (operation_ == operation_t::READ_WITH_DATA) {
			opType = 'D';
		} else if (operation_ == operation_t::WRITE) {
			opType = 'W';
		}

		DBGLOGEMS("(0x%X) -%c-> (0x%X), type: 0x%4.4X, offset: %d, dataLen: %d data: ", source_, opType, destination_, typeId_, offset_, data_.size());
		for (size_t i = 0; i < data_.size(); ++i) {
			logger.printf("%2.2X ", data_[i]);
		}
		logger.printf("\n");
	}
}

uint16_t EmsTelegram::getTelegramTypeFromRaw(uint8_t *data, uint8_t length) {
	if (data[2] != 0xFF || length < 6) { // EMS1
		return data[2];
	} else if (data[1] & 0x80) { // EMS2.0 read request
		return (data[5] << 8) + data[6] + 256;
	} else { // EMS2.0/EMS+
		return (data[4] << 8) + data[5] + 256;
	}
}

EmsTelegram EmsTelegram::getFromRawData(uint8_t *data, uint8_t length) { // decodes full raw telegram, data without tailing BRK \0, throws
	if (data[length - 1] != calculateCRC(data, length - 1)) {
		throw std::runtime_error("Bad CRC");
	}

	operation_t operation = EmsTelegram::operation_t::READ;

	uint8_t destination = data[1]; // 0 - broadcast
	if (destination == 0) {
		operation = operation_t::BROADCAST;
	} else {
		operation = destination & 0x80 ? operation_t::READ : operation_t::WRITE;
	}

	if (data[2] != 0xFF || length < 6) { // EMS1
		DBGLOGEMSVB("Got EMS1.0 raw len: %d from: %d\n", length, data[0]); // might be executed on uart thread - log only in verbose mode
		return EmsTelegram(operation, data[0], destination, data[3], data[2], data + 4, length - 5);
	} else if (data[1] & 0x80) { // EMS2.0 read request
		DBGLOGEMSVB("Got EMS2.0 read request raw len: %d\n", length);
		return EmsTelegram(destination == 0 ? EmsTelegram::operation_t::BROADCAST : EmsTelegram::operation_t::READ, data[0], destination, data[3], (data[5] << 8) + data[6] + 256, data + 4, 1);
	} else { // EMS2.0/EMS+
		DBGLOGEMSVB("Got EMS2.0 raw len: %d\n", length);
		return EmsTelegram(destination == 0 ? EmsTelegram::operation_t::BROADCAST : EmsTelegram::operation_t::WRITE, data[0], destination, data[3], (data[4] << 8) + data[5] + 256, data + 6, length - 7);
	}
}

std::vector<uint8_t> EmsTelegram::encodeToRawDataWithCRC() const {
	std::vector<uint8_t> rawData;
	static constexpr uint8_t maxRawTelegramLength = 32;
	rawData.reserve(maxRawTelegramLength); // TODO calculate how much buffer we need

	// TODO check buffer overflow if there is too much data

	rawData.push_back(source_ | 0x80); // 0
	rawData.push_back(destination_);   // 1
	if ((operation_ == operation_t::READ || operation_ == operation_t::READ_WITH_DATA) && destination_ != 0) {
		rawData[1] |= 0x80;
	}

	rawData.push_back(typeId_ > 0xFF ? 0xFF : typeId_); // 2 - typeId for EMS1 -  0xFF marker for EMS2
	rawData.push_back(offset_);							// 4

	uint8_t crcPos = 0;
	if (typeId_ > 0xFF) {									 // EMS2
		if (operation_ == operation_t::READ) {				 // READ REQUEST
			rawData.push_back(data_.empty() ? 0 : data_[0]); // 4 message data - requested length
			rawData.push_back((typeId_ >> 8) - 1);			 // 5 // type, 1st byte, high-byte, subtract 0x100
			rawData.push_back(typeId_ & 0xFF);				 // 6// type, 2nd byte, low-byte
		} else {											 // write
			rawData.push_back((typeId_ >> 8) - 1);			 // 4
			rawData.push_back(typeId_ & 0xFF);				 // 5
			rawData.insert(rawData.end(), data_.begin(), data_.end());
		}
	} else {												 // EMS 1.0
		if (operation_ == operation_t::READ) {				 // READ REQUEST
			rawData.push_back(data_.empty() ? 0 : data_[0]); //  message data - requested length
		} else {
			rawData.insert(rawData.end(), data_.begin(), data_.end());
		}
	}

	rawData.push_back(calculateCRC(rawData.data(), rawData.size()));
	return rawData;
}

int8_t EmsTelegram::getDataOffset(uint8_t dataStart, uint8_t dataLen) const {
	if (offset_ > dataStart || dataStart - offset_ + dataLen > data_.size()) {
		return -1;
	}
	return dataStart - offset_;
}

uint8_t EmsTelegram::calculateCRC(const uint8_t *data, const uint8_t length) { // from EMS-ESP32
	uint8_t i = 0;
	uint8_t crc = 0;
	while (i < length) {
		crc = ems_crc_table[crc];
		crc ^= data[i++];
	}
	return crc;
}

} // namespace heating
