#include <gtest/gtest.h>
#include "BoilerConfigEndpoint.h"
#include "BoilerConfigStorage.h"
#include "BoilerController.h"
#include <cstdlib>

namespace {
struct Server {
 HTTPMethod verb=HTTP_POST;
 bool hasBody=true;
 String body;
 int status=0;
 std::string response;
 HTTPMethod method() { return verb; }
 bool hasArg(const char *) { return hasBody; }
 String arg(const char *) { return body; }
 void send(int code,const char * = "", const char *text="") { status=code; response=text; }
 void streamFile(File &file,const char *) { status=200; response=file.readString(); }
};
const char *valid=R"({"minHeatingCurveTemp":20,"maxHeatingCurveTemp":75,"heatingCurve":[20,25,30,35,45,55,65,75,80],"boiler":{"valvePreheatingDelay":30,"minHeatingTemp":20,"maxHeatingTemp":75,"controlMode":"ems","outdoorSensor":"owm","supplyMode":"fixed","fixedSupplyTemperature":48,"weatherFallback":false}})";
class BoilerEndpoint : public testing::Test {
protected:
 NativeFilesystem fs;
 Server server;
 void SetUp() override {
  char dir[]="/tmp/thermostat-config-XXXXXX";
  fs.root=mkdtemp(dir);
  std::filesystem::create_directories(fs.root/"cfg");
  server.body=valid;
 }
 void TearDown() override { std::filesystem::remove_all(fs.root); }
 void request() { heating::handleBoilerConfiguration(server,fs); }
};
TEST_F(BoilerEndpoint, PostGetAndRebootUsePersistedPolicyWithoutReloadingLiveSettings) {
 request(); ASSERT_EQ(server.status,201);
 const auto running=config::loadBoilerConfiguration(fs);
 ASSERT_EQ(running.boiler.fixedSupplyTemperature,48);
 auto next=std::string(valid); next.replace(next.find(":48"),3,":52"); server.body=next;
 request(); ASSERT_EQ(server.status,201);
 EXPECT_EQ(running.boiler.fixedSupplyTemperature,48); // Running copy remains unchanged.
 server.verb=HTTP_GET; request(); ASSERT_EQ(server.status,200); EXPECT_EQ(server.response,next);
 NativeFilesystem rebooted; rebooted.root=fs.root;
 const auto boot=config::loadBoilerConfiguration(rebooted);
 EXPECT_EQ(boot.boiler.fixedSupplyTemperature,52);
 EXPECT_EQ(boot.boiler.controlMode,config::BoilerConfig::controlMode_t::ems);
 EXPECT_FALSE(boot.boiler.weatherFallback);
 EXPECT_EQ(heating::resolveSupply(boot,{}).requested,52);
}
TEST_F(BoilerEndpoint, InvalidPostCannotTruncatePreviouslySavedConfig) {
 request(); ASSERT_EQ(server.status,201);
 std::vector<std::string> invalid={"{","{}","null"};
 auto unsupported=std::string(valid); unsupported.replace(unsupported.find("fixed"),5,"boiler");
 invalid.push_back(unsupported);
 auto missingFallback=std::string(valid); missingFallback.replace(missingFallback.find(":48"),3,":null");
 invalid.push_back(missingFallback);
 for (const auto &body : invalid) {
  server.verb=HTTP_POST; server.body=body; request(); EXPECT_EQ(server.status,400);
  EXPECT_EQ(fs.writes,1u);
  server.verb=HTTP_GET; request(); EXPECT_EQ(server.response,valid);
  EXPECT_EQ(config::loadBoilerConfiguration(fs).boiler.fixedSupplyTemperature,48);
 }
}
TEST_F(BoilerEndpoint, TrailingDataIsRejectedWithoutChangingTheSavedDocument) {
	request(); ASSERT_EQ(server.status, 201);
	std::vector<std::string> suffixes = {"trailing", " {}", " null", "\v", "\f", std::string("\0junk", 5)};
	for (const auto &suffix : suffixes) {
		server.verb = HTTP_POST; server.body = std::string(valid) + suffix;
		request(); EXPECT_EQ(server.status, 400);
		EXPECT_EQ(fs.writes, 1u);
		server.verb = HTTP_GET; request(); EXPECT_EQ(server.status, 200);
		EXPECT_EQ(server.response, valid);
		EXPECT_EQ(config::loadBoilerConfiguration(fs).boiler.fixedSupplyTemperature, 48);
	}
}
TEST_F(BoilerEndpoint, JsonWhitespaceSurvivesPostGetAndReboot) {
	server.body = std::string(" \r\n\t") + valid + " \r\n\t";
	request(); ASSERT_EQ(server.status, 201);
	server.verb = HTTP_GET; request(); EXPECT_EQ(server.response, server.body);
	EXPECT_EQ(config::loadBoilerConfiguration(fs).boiler.fixedSupplyTemperature, 48);
}

TEST_F(BoilerEndpoint, ControlBytesAreRejectedAcrossTheWholeDocumentBeforeWriting) {
	request(); ASSERT_EQ(server.status, 201);
	for (int byte = 0; byte < 0x20; ++byte) {
		const bool whitespace = byte == '\t' || byte == '\r' || byte == '\n';
		for (const auto position : {size_t{0}, std::string(valid).find("\"boiler\"")}) {
			if (whitespace) continue; // Legal whitespace outside strings is tested below.
			auto malformed = std::string(valid); malformed.insert(position, 1, static_cast<char>(byte));
			server.verb = HTTP_POST; server.body = malformed;
			request(); EXPECT_EQ(server.status, 400) << "byte=" << byte << " position=" << position;
			EXPECT_EQ(fs.writes, 1u);
			server.verb = HTTP_GET; request(); EXPECT_EQ(server.response, valid);
			EXPECT_EQ(config::loadBoilerConfiguration(fs).boiler.fixedSupplyTemperature, 48);
		}
		// Even tab/CR/LF are forbidden as raw bytes inside string values.
		auto malformed = std::string(valid);
		malformed.insert(1, std::string("\"note\":\"raw") + static_cast<char>(byte) + "value\",");
		server.verb = HTTP_POST; server.body = malformed;
		request(); EXPECT_EQ(server.status, 400) << "string byte=" << byte;
		EXPECT_EQ(fs.writes, 1u);
		server.verb = HTTP_GET; request(); EXPECT_EQ(server.response, valid);
		EXPECT_EQ(config::loadBoilerConfiguration(fs).boiler.fixedSupplyTemperature, 48);
	}
}
TEST_F(BoilerEndpoint, LegalInteriorWhitespaceEscapesAndUtf8RemainReadableAfterRestart) {
	auto body = std::string(valid);
	body.insert(body.find("\"boiler\""), " \t\r\n");
	body.insert(1, R"("note":"Zażółć \t \n \r \" \\ \u0001",)");
	server.body = body; request(); ASSERT_EQ(server.status, 201);
	server.verb = HTTP_GET; request(); ASSERT_EQ(server.status, 200); EXPECT_EQ(server.response, body);
	const char *end = nullptr;
	auto parsed = cJSON_ParseWithOpts(server.response.c_str(), &end, true);
	ASSERT_NE(parsed, nullptr); cJSON_Delete(parsed);
	EXPECT_EQ(config::loadBoilerConfiguration(fs).boiler.fixedSupplyTemperature, 48);
}

TEST_F(BoilerEndpoint, NonStringWeatherSourcesAreRejectedBeforePersistence) {
	request(); ASSERT_EQ(server.status, 201);
	for (const auto value : {"false", "42", "null", "[]", "{}"}) {
		auto malformed = std::string(valid);
		const auto position = malformed.find("\"owm\"");
		malformed.replace(position, 5, value);
		server.verb = HTTP_POST; server.body = malformed;
		request(); EXPECT_EQ(server.status, 400) << value;
		EXPECT_EQ(fs.writes, 1u);
		server.verb = HTTP_GET; request(); EXPECT_EQ(server.response, valid);
	}
	// Missing-property legacy behavior is still supported.
	auto root = cJSON_Parse(valid);
	cJSON_DeleteItemFromObject(cJSON_GetObjectItem(root, "boiler"), "outdoorSensor");
	auto text = cJSON_PrintUnformatted(root); server.body = text;
	cJSON_free(text); cJSON_Delete(root);
	server.verb = HTTP_POST; request(); EXPECT_EQ(server.status, 201);
	EXPECT_EQ(config::loadBoilerConfiguration(fs).boiler.outdoorSensor, config::BoilerConfig::outdoorSensor_t::no);
}

TEST_F(BoilerEndpoint, LegacyPolicySurvivesSaveAndRestartWithoutInventedFallback) {
 auto root=cJSON_Parse(valid);
 auto boiler=cJSON_GetObjectItem(root,"boiler");
 cJSON_DeleteItemFromObject(boiler,"supplyMode");
 cJSON_DeleteItemFromObject(boiler,"fixedSupplyTemperature");
 auto text=cJSON_PrintUnformatted(root); server.body=text; cJSON_free(text); cJSON_Delete(root);
 request(); ASSERT_EQ(server.status,201);
 NativeFilesystem rebooted; rebooted.root=fs.root;
 const auto boot=config::loadBoilerConfiguration(rebooted);
 EXPECT_EQ(boot.boiler.controlMode,config::BoilerConfig::controlMode_t::ems);
 EXPECT_FALSE(boot.boiler.fixedSupplyTemperature);
 EXPECT_FALSE(heating::resolveSupply(boot,{}).base);
 EXPECT_EQ(heating::resolveSupply(boot,{500,"openweather","fresh"}).base,35);
}
TEST_F(BoilerEndpoint, MissingOrCorruptStoredDataUsesDefaults) {
 EXPECT_EQ(config::loadBoilerConfiguration(fs).boiler.controlMode,config::BoilerConfig::controlMode_t::onoff);
 auto file=fs.open("/cfg/cfgboiler.json",FILE_WRITE);
 std::string corrupt="{";
 file.write(reinterpret_cast<uint8_t *>(corrupt.data()),corrupt.size());file.close();
 EXPECT_FALSE(config::loadBoilerConfiguration(fs).boiler.fixedSupplyTemperature);
 EXPECT_EQ(config::loadBoilerConfiguration(fs).boiler.controlMode,config::BoilerConfig::controlMode_t::onoff);
}
TEST_F(BoilerEndpoint, EmptyBodyAndFilesystemFailuresHaveExpectedResponses) {
 server.hasBody=false; request(); EXPECT_EQ(server.status,204); EXPECT_EQ(fs.writes,0u);
 server.hasBody=true; server.body=""; request(); EXPECT_EQ(server.status,204); EXPECT_EQ(fs.writes,0u);
 server.verb=HTTP_GET; request(); EXPECT_EQ(server.status,404);
 server.verb=HTTP_POST; server.body=valid; fs.failOpen=true; request(); EXPECT_EQ(server.status,500);
}
}
