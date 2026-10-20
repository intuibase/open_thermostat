#pragma once

#include <atomic>
#include <cstdint>
#include <ostream>

namespace heating::ems {

// exchange() may race with observe(): retrying CAS preserves the new window.
class EmsMaximum {
public:
	void observe(uint32_t value) {
		auto previous = value_.load(std::memory_order_relaxed);
		while (previous < value && !value_.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {}
	}
	uint32_t load() const { return value_.load(std::memory_order_relaxed); }
	uint32_t take() { return value_.exchange(0, std::memory_order_relaxed); }
private:
	std::atomic<uint32_t> value_{0};
};

struct EmsUartStatsSnapshot {
	uint32_t frames, bytes, readErrors, invalidLengths, fifoOverflows, bufferFull;
	uint32_t parityErrors, frameErrors, timeouts, resets, writeErrors;
};

struct EmsControllerStatsSnapshot {
	uint32_t polls, txFrames, pongs, rxDropped, txDropped, decodeErrors, slowPolls;
	uint32_t rxHighWater, txHighWater, maxPollStartUs, maxPollServiceUs;
	uint32_t slowPollThresholdUs;
};

// Independent relaxed loads: telemetry, not a transactional snapshot.
struct EmsUartStats {
	std::atomic<uint32_t> frames{0}, bytes{0}, readErrors{0}, invalidLengths{0};
	std::atomic<uint32_t> fifoOverflows{0}, bufferFull{0}, parityErrors{0}, frameErrors{0};
	std::atomic<uint32_t> timeouts{0}, resets{0}, writeErrors{0};
	EmsUartStatsSnapshot snapshot() const {
		return {frames.load(std::memory_order_relaxed), bytes.load(std::memory_order_relaxed),
			readErrors.load(std::memory_order_relaxed), invalidLengths.load(std::memory_order_relaxed),
			fifoOverflows.load(std::memory_order_relaxed), bufferFull.load(std::memory_order_relaxed),
			parityErrors.load(std::memory_order_relaxed), frameErrors.load(std::memory_order_relaxed),
			timeouts.load(std::memory_order_relaxed), resets.load(std::memory_order_relaxed),
			writeErrors.load(std::memory_order_relaxed)};
	}
};

// Counters are cumulative; maxima describe the last completed reporting window.
// Serialize only from loop()/MQTT, never from the UART task.
inline void writeEmsDiagnostics(std::ostream &ss, const EmsControllerStatsSnapshot &controller,
	const EmsUartStatsSnapshot &uart, uint32_t echoPending, uint32_t forwarderErrors,
	uint32_t forwarderDropped, uint32_t windowEndMs, uint32_t windowDurationMs,
	uint32_t queueCapacity, uint32_t frameCapacity, uint32_t forwarderCapacity,
	uint32_t forwarderDeviceCapacity, bool enabled) {
	ss << "{\"enabled\":" << (enabled ? "true" : "false");
	auto field = [&](const char *name, uint32_t value) { ss << ",\"" << name << "\":" << value; };
	field("poll", controller.polls);
	field("tx", controller.txFrames);
	field("pong", controller.pongs);
	field("rx_drop", controller.rxDropped);
	field("tx_drop", controller.txDropped);
	field("decode_err", controller.decodeErrors);
	field("slow_poll", controller.slowPolls);
	field("slow_threshold_us", controller.slowPollThresholdUs);
	field("rx_hwm", controller.rxHighWater);
	field("tx_hwm", controller.txHighWater);
	field("echo_pending", echoPending);
	field("rx", uart.frames);
	field("rx_bytes", uart.bytes);
	field("read_err", uart.readErrors);
	field("length_err", uart.invalidLengths);
	field("fifo_ovf", uart.fifoOverflows);
	field("buffer_full", uart.bufferFull);
	field("parity_err", uart.parityErrors);
	field("frame_err", uart.frameErrors);
	field("timeout", uart.timeouts);
	field("resets", uart.resets);
	field("write_err", uart.writeErrors);
	field("fwd_err", forwarderErrors);
	field("fwd_drop", forwarderDropped);
	field("window_end_uptime_ms", windowEndMs);
	field("window_duration_ms", windowDurationMs);
	field("rx_capacity", queueCapacity);
	field("tx_capacity", queueCapacity);
	field("frame_capacity", frameCapacity);
	field("fwd_capacity", forwarderCapacity);
	field("fwd_device_capacity", forwarderDeviceCapacity);
	ss << ",\"poll_start_max_us\":";
	if (windowDurationMs) { ss << controller.maxPollStartUs; } else { ss << "null"; }
	ss << ",\"poll_service_max_us\":";
	if (windowDurationMs) { ss << controller.maxPollServiceUs; } else { ss << "null"; }
	ss << "}";
}

} // namespace heating::ems
