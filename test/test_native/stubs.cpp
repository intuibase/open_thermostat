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

#include "config.h"
namespace json {
std::string getString(cJSON *root,const char *name) {
 auto item=cJSON_GetObjectItem(root,name); return cJSON_IsString(item)?item->valuestring:"";
}
int getInt(cJSON *root,const char *name) {
 auto item=cJSON_GetObjectItem(root,name); return cJSON_IsNumber(item)?item->valueint:0;
}
float getFloat(cJSON *root,const char *name) {
 auto item=cJSON_GetObjectItem(root,name); return cJSON_IsNumber(item)?item->valuedouble:0;
}
}
namespace ib {
uint64_t getTimeMillis() { return 1793808000000ULL; }
}
