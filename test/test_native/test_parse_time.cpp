#include <gtest/gtest.h>
#include "TimeUtils.h"

namespace {

using ib::timeutils::parseTimeHHMM;

// ============================================================================
// Valid inputs
// ============================================================================

TEST(ParseTimeHHMM, Midnight) {
	EXPECT_EQ(parseTimeHHMM("00:00"), 0);
}

TEST(ParseTimeHHMM, EndOfDay) {
	EXPECT_EQ(parseTimeHHMM("23:59"), 2359);
}

TEST(ParseTimeHHMM, Morning) {
	EXPECT_EQ(parseTimeHHMM("06:30"), 630);
}

TEST(ParseTimeHHMM, Noon) {
	EXPECT_EQ(parseTimeHHMM("12:00"), 1200);
}

TEST(ParseTimeHHMM, SingleDigitMinutes) {
	EXPECT_EQ(parseTimeHHMM("08:05"), 805);
}

// ============================================================================
// Invalid format — should throw invalid_argument
// ============================================================================

TEST(ParseTimeHHMM, EmptyString) {
	EXPECT_THROW(parseTimeHHMM(""), std::invalid_argument);
}

TEST(ParseTimeHHMM, TooShort) {
	EXPECT_THROW(parseTimeHHMM("8:30"), std::invalid_argument);
}

TEST(ParseTimeHHMM, TooLong) {
	EXPECT_THROW(parseTimeHHMM("08:300"), std::invalid_argument);
}

TEST(ParseTimeHHMM, NoDivider) {
	EXPECT_THROW(parseTimeHHMM("08300"), std::invalid_argument);
}

TEST(ParseTimeHHMM, WrongDivider) {
	EXPECT_THROW(parseTimeHHMM("08-30"), std::invalid_argument);
}

TEST(ParseTimeHHMM, Letters) {
	EXPECT_THROW(parseTimeHHMM("ab:cd"), std::exception);
}

// ============================================================================
// Out of range — should throw out_of_range
// ============================================================================

TEST(ParseTimeHHMM, Hour24) {
	EXPECT_THROW(parseTimeHHMM("24:00"), std::out_of_range);
}

TEST(ParseTimeHHMM, Hour99) {
	EXPECT_THROW(parseTimeHHMM("99:00"), std::out_of_range);
}

TEST(ParseTimeHHMM, Minute60) {
	EXPECT_THROW(parseTimeHHMM("08:60"), std::out_of_range);
}

TEST(ParseTimeHHMM, Minute99) {
	EXPECT_THROW(parseTimeHHMM("08:99"), std::out_of_range);
}

} // anonymous namespace
