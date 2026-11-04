// Stubs for Arduino-dependent symbols
#include <gtest/gtest.h>
#include "Logging.h"

namespace heating {
std::shared_ptr<ib::logger::LoggerInterface> logger;
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
