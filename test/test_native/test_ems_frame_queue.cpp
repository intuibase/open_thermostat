#include "EMS/EmsFrameQueue.h"
#include <gtest/gtest.h>
#include <thread>

namespace {
using heating::ems::EmsFrameQueue;
using heating::ems::EmsRawFrame;

TEST(EmsFrameQueue, OwnsCopyOfUartBuffer) {
	EmsFrameQueue<2> queue;
	uint8_t uart[] = {0x88, 0x19, 0x18, 0, 10, 20, 0};
	const auto original = std::to_array(uart);
	ASSERT_TRUE(queue.push(uart, sizeof(uart)));
	std::fill(std::begin(uart), std::end(uart), 0xFF);
	EmsRawFrame frame;
	ASSERT_TRUE(queue.pop(frame));
	EXPECT_EQ(frame.length, original.size());
	EXPECT_TRUE(std::equal(original.begin(), original.end(), frame.data.begin()));
	EXPECT_FALSE(queue.pop(frame));
}

TEST(EmsFrameQueue, FullQueuePreservesPendingFramesAndRecoversAfterWrap) {
	EmsFrameQueue<3> queue;
	EmsRawFrame frame;
	for (unsigned cycle = 0; cycle < 100; ++cycle) {
		for (uint8_t value = 0; value < 3; ++value) {
			ASSERT_TRUE(queue.push(&value, 1));
		}
		const uint8_t overflow = 99;
		EXPECT_FALSE(queue.push(&overflow, 1));
		for (uint8_t value = 0; value < 3; ++value) {
			ASSERT_TRUE(queue.pop(frame));
			EXPECT_EQ(frame.data[0], value);
		}
		EXPECT_FALSE(queue.pop(frame));
	}
}

TEST(EmsFrameQueue, RejectsInvalidLengthsAndAcceptsMaximumFrame) {
	EmsFrameQueue<1> queue;
	std::array<uint8_t, EmsRawFrame::capacity + 1> data{};
	EXPECT_FALSE(queue.push(data.data(), 0));
	EXPECT_FALSE(queue.push(data.data(), data.size()));
	EXPECT_TRUE(queue.push(data.data(), EmsRawFrame::capacity));
	EmsRawFrame frame;
	ASSERT_TRUE(queue.pop(frame));
	EXPECT_EQ(frame.length, EmsRawFrame::capacity);
}

TEST(EmsFrameQueue, ConcurrentProducerAndConsumerPreserveOrderAndPayload) {
	EmsFrameQueue<7> queue;
	constexpr uint32_t count = 100000;
	std::thread producer([&] {
		for (uint32_t i = 0; i < count; ++i) {
			std::array<uint8_t, EmsRawFrame::capacity> bytes;
			for (size_t j = 0; j < bytes.size(); ++j) {
				bytes[j] = static_cast<uint8_t>((i >> ((j % 4) * 8)) ^ j);
			}
			while (!queue.push(bytes.data(), bytes.size())) {
				std::this_thread::yield();
			}
		}
	});
	bool valid = true;
	for (uint32_t i = 0; i < count; ++i) {
		EmsRawFrame frame;
		while (!queue.pop(frame)) {
			std::this_thread::yield();
		}
		valid &= frame.length == EmsRawFrame::capacity;
		for (size_t j = 0; j < frame.data.size(); ++j) {
			valid &= frame.data[j] == static_cast<uint8_t>((i >> ((j % 4) * 8)) ^ j);
		}
	}
	producer.join();
	EXPECT_TRUE(valid);
}
} // namespace
