#include "EMS/EmsDiagnostics.h"
#include <cJSON.h>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>

TEST(EmsDiagnostics, SerializesCountersLimitsAndLastWindowWithoutResettingThem) {
	heating::ems::EmsControllerStatsSnapshot controller{};
	heating::ems::EmsUartStatsSnapshot uart{};
	controller.polls = UINT32_MAX;
	controller.pongs = 7;
	controller.maxPollStartUs = 42;
	controller.maxPollServiceUs = 18000;
	uart.fifoOverflows = 3;
	auto serialize = [&] {
		std::ostringstream ss;
		heating::ems::writeEmsDiagnostics(ss, controller, uart, 2, 4, 5,
			60000, 30000, 59, 33, 32, 10, true);
		return ss.str();
	};
	const auto payload = serialize();
	EXPECT_EQ(payload, serialize());
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(cJSON_Parse(payload.c_str()), cJSON_Delete);
	ASSERT_NE(json, nullptr);
	auto number = [&](const char *key) {
		const auto *value = cJSON_GetObjectItemCaseSensitive(json.get(), key);
		EXPECT_TRUE(cJSON_IsNumber(value)) << key;
		return cJSON_GetNumberValue(value);
	};
	EXPECT_EQ(number("poll"), static_cast<double>(UINT32_MAX));
	EXPECT_EQ(number("pong"), 7);
	EXPECT_EQ(number("fifo_ovf"), 3);
	EXPECT_EQ(number("echo_pending"), 2);
	EXPECT_EQ(number("rx_capacity"), 59);
	EXPECT_EQ(number("tx_capacity"), 59);
	EXPECT_EQ(number("frame_capacity"), 33);
	EXPECT_EQ(number("fwd_capacity"), 32);
	EXPECT_EQ(number("fwd_device_capacity"), 10);
	EXPECT_EQ(number("poll_start_max_us"), 42);
	EXPECT_EQ(number("poll_service_max_us"), 18000);
	EXPECT_EQ(number("window_duration_ms"), 30000);
}

TEST(EmsDiagnostics, DoesNotReportUnmeasuredStartupTimingAsZero) {
	std::ostringstream ss;
	heating::ems::writeEmsDiagnostics(ss, {}, {}, 0, 0, 0, 0, 0, 59, 33, 32, 10, false);
	std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(cJSON_Parse(ss.str().c_str()), cJSON_Delete);
	ASSERT_NE(json, nullptr);
	EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json.get(), "enabled")));
	EXPECT_TRUE(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(json.get(), "poll_start_max_us")));
	EXPECT_TRUE(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(json.get(), "poll_service_max_us")));
}
