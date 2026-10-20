#pragma once

#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include "EmsDiagnostics.h"

namespace heating::ems {

struct EmsRawFrame {
	static constexpr size_t capacity = 33;
	std::array<uint8_t, capacity> data{};
	uint8_t length = 0;
};

// Exactly one producer (UART) and one consumer (loop). No allocation or waiting.
template<size_t Capacity>
class EmsFrameQueue {
public:
	bool push(const uint8_t *data, size_t length) {
		if (length == 0 || length > EmsRawFrame::capacity) {
			return false;
		}
		const auto write = write_.load(std::memory_order_relaxed);
		const auto next = (write + 1) % slots_.size();
		const auto read = read_.load(std::memory_order_acquire);
		if (next == read) {
			return false;
		}
		auto &frame = slots_[write];
		std::copy_n(data, length, frame.data.begin());
		frame.length = static_cast<uint8_t>(length);
		// Occupancy at the producer's read-index sample, before publishing.
		// Sampling after publication could miss an immediately consumed frame.
		highWater_.observe(static_cast<uint32_t>((next + slots_.size() - read) % slots_.size()));
		write_.store(next, std::memory_order_release);
		return true;
	}

	uint32_t highWaterMark() const { return highWater_.load(); }

	bool pop(EmsRawFrame &frame) {
		const auto read = read_.load(std::memory_order_relaxed);
		if (read == write_.load(std::memory_order_acquire)) {
			return false;
		}
		frame = slots_[read];
		read_.store((read + 1) % slots_.size(), std::memory_order_release);
		return true;
	}

private:
	static_assert(Capacity > 0);
	std::array<EmsRawFrame, Capacity + 1> slots_{};
	std::atomic<size_t> read_{0};
	std::atomic<size_t> write_{0};
	EmsMaximum highWater_;
};

} // namespace heating::ems
