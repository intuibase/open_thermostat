#pragma once
#include "Logging.h"

#include <esp_ota_ops.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <Wire.h>
#include <uri/UriBraces.h>
#include <cJSON.h>

#include "viewable_stringbuf.h"
#include "HeatingController.h"

#include "Network.h"
#include <iomanip>
#include <memory>
#include <sstream>
#include <string_view>

namespace heating {

using namespace std::string_view_literals;
using namespace std::string_literals;

class WebServerStringView : public WebServer {
public:
	WebServerStringView(uint16_t listenPort) : WebServer(listenPort) {

	}

	void sendView(int code, std::string_view content_type, std::string_view content) {
		String header;
		if (content.length() == 0) {
			log_w("content length is zero");
		}

		_prepareHeader(header, code, content_type.data(), content.length());
		_currentClientWrite(header.c_str(), header.length());

		if(content.length()) {
			sendContent(content.data(), content.length());
		}
	}

	void sendBytes(const char *bytes, size_t size) { _currentClientWrite(bytes, size); }
	void endResponse() { _finalizeResponse(); }

	HTTPUpload *getUpload() { return _currentUpload.get(); }
};

class REST {
public:
	REST(std::shared_ptr<ib::logger::LoggerInterface> log, HeatingController &controller, uint16_t listenPort) : log_(std::move(log)), controller_(controller), server_(listenPort) {
		if (log_) {
			static const auto id = log_->addFeature("REST");
			logFeature_ = id;
		}
		server_.enableCORS(true);
		server_.enableCrossOrigin(true);

		server_.on("/status/wifi", [this]() {
			DBGLOGFD(log_, logFeature_, "REST:wifiNetworks\n");

			ib::viewable_stringbuf payloadBuf;
			std::ostream payload(&payloadBuf);

			getWiFiNetworks(payload);
			server_.sendView(200, "application/json"sv, payloadBuf.view());
		});

		server_.on("/status", [this]() { status(); });
		server_.on("/status/boiler", [this]() { boilerStatus(); });
		server_.on("/status/ems", [this]() { emsStatus(); });
		server_.on("/status/rooms", [this]() { roomsStatus(); });
		server_.on("/status/devices", [this]() { devicesFound(); });
		server_.on("/status/programs", HTTP_GET, [this]() { showPrograms(); }); // show available programs
		server_.on("/status/version", [this]() { version(); });
		server_.on("/params/boiler", [this]() { emsParams(); });


		server_.on("/config/temporary", HTTP_POST, [this]() { temporaryOverride(); }); // GET/POST/DELETE name without .json, case sensitive

		server_.on(UriBraces("/config/programs/{}"), [this]() { configPrograms(); }); // GET/POST/DELETE name without .json, case sensitive
		server_.on("/config/wifi", [this]() { configWiFi(); });
		server_.on("/config/device", [this]() { configDevice(); });
		server_.on("/config/boiler", [this]() { configBoiler(); });
		server_.on("/config/hardware", [this]() { configHardware(); });
		server_.on("/hardware/i2c/scan", HTTP_GET, [this]() { i2cScan(); });
		server_.on("/hardware/test/gpio", HTTP_POST, [this]() { gpioTestStart(); });
		server_.on("/hardware/test/gpio", HTTP_DELETE, [this]() { gpioTestStop(); });
		server_.on("/config/debug", [this]() { configDebug(); });

		server_.on("/config/program/current", [this]() { configProgramCurrent(); }); // get selected program
		server_.on("/config/reboot", HTTP_GET, [this]() { configReboot(); });

		server_.on("/ota", HTTP_POST, [this]() { handleOTAResponse(); }, [this]() { handleOTAUpdate(); });
		server_.on("/otafs", HTTP_POST, [this]() { handleOTAResponse(); }, [this]() { handleOTAFFSUpdate(); });

		server_.on("/", HTTP_GET, [this]() { index(); });

		server_.onNotFound([this]() {
			serveFile(server_.uri().c_str());
		});
	}

	void restart() {
		server_.stop();
		server_.begin();
	}

	void handle() {
		server_.handleClient();
	}

private:
	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_{};
	// bool auth() {
	// 	if (!server_.authenticate("admin", "admin")) {
	// 		server_.requestAuthentication(DIGEST_AUTH);
	// 		return false;
	// 	}
	// 	return true;
	// }

	void showPrograms() {
		DBGLOGFD(log_, logFeature_, "showPrograms\n");

		ib::viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);

		ss << "[";
		File path = SPIFFS.open("/programs");
		bool first = true;
		while (path) {
			using namespace std::string_view_literals;
			static constexpr auto jsonExtension = ".json"sv;

			auto file = path.openNextFile();
			if (!file) {
				break;
			}
			DBGLOGFD(log_, logFeature_, "showPrograms '%s'\n", file.name());

			if (file.isDirectory()) {
				DBGLOGFD(log_, logFeature_, "showPrograms skip directory: %s\n", file.name());
				continue;
			}

			std::string name = file.name();
			auto pos = name.find(jsonExtension);
			if (pos == std::string::npos) {
				DBGLOGFD(log_, logFeature_, "showPrograms skip non-json: %s\n", file.name());
				continue;
			}
			name = name.substr(0, name.length() - jsonExtension.length());

			if (first) {
				ss << "\"" << name << "\"";
				first = false;
			} else {
				ss << ", \"" << name << "\"";
			}
		}

		path.close();
		ss << "]";
		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void configProgramCurrent() {
		DBGLOGFD(log_, logFeature_, "configProgramCurrent method: %d\n", server_.method());

		switch (server_.method()) {
			case HTTP_GET: {
				File file = SPIFFS.open("/cfg/cfgprogram.json", FILE_READ);
				if (!file) {
					DBGLOGFD(log_, logFeature_, "configProgramCurrent missing config");
					server_.send(500, "text/plain", "Missing config");
					return;
				}
				server_.streamFile(file, "application/json");
				file.close();
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(500, "text/plain", "missing body");
					return;
				}
				auto body = server_.arg("plain");

				auto program = config::parseProgram(body.c_str());
				if (program.empty()) {
					DBGLOGFD(log_, logFeature_, "configProgramCurrent error parsing program\n");
					server_.send(400, "text/html", "Program parsing failure. Config not modified");
					return;
				}

				std::string filename = "/programs/" + program + ".json";

				if (!SPIFFS.exists(filename.c_str())) {
					DBGLOGFD(log_, logFeature_, "Received new program configuration: '%s'. Program does not exists!\n", program.c_str());
					server_.send(400, "text/html", "Program does not exists. Config not modified");
					return;
				}

				DBGLOGFD(log_, logFeature_, "Received new program configuration: '%s'\n", program.c_str());
				File file = SPIFFS.open("/cfg/cfgprogram.json", FILE_WRITE);
				if (!file) {
					server_.send(500, "text/html", "Filesystem failure. Unable to write program.");
					return;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();

				server_.send(204);
				controller_.reloadConfiguration();
				break;
			}
			default:
				server_.sendHeader("Allow", "GET, POST");
				server_.send(405);
				break;
		}


	}

	void configReboot() {
		DBGLOGFD(log_, logFeature_, "configReboot\n");
		server_.send(200);
		server_.stop();
		ESP.restart();
	}

	void boilerStatus() {
		DBGLOGFD(log_, logFeature_, "boilerStatus\n");

		ib::viewable_stringbuf payloadBuf;
		std::ostream payload(&payloadBuf);

		controller_.getBoilerStatus(payload);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void emsStatus() {
		DBGLOGFD(log_, logFeature_, "emsStatus\n");

		ib::viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		controller_.getEMSStatus(ss);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void emsParams() {
		DBGLOGFD(log_, logFeature_, "emsParams\n");
		ib::viewable_stringbuf payloadBuf;
		std::ostream payload(&payloadBuf);
		controller_.getEMSBoilerParams(payload);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void status() {
		DBGLOGFD(log_, logFeature_, "status\n");
		server_.enableCORS(true);
		ib::viewable_stringbuf payloadBuf;
		std::ostream payload(&payloadBuf);
		controller_.getFullStatus(payload);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void version() {
		DBGLOGFD(log_, logFeature_, "version\n");

		const esp_app_desc_t *app = esp_ota_get_app_description();

		ib::viewable_stringbuf payloadBuf;
		std::ostream payload(&payloadBuf);
		payload << '{';
		payload << "\"project\": \""sv << app->project_name << "\","sv;
		payload << "\"version\": \""sv << app->version << "\","sv;
		payload << "\"date\": \""sv << app->date << "\","sv;
		payload << "\"time\": \""sv << app->time << "\","sv;
		payload << "\"idf\": \""sv << app->idf_ver << "\","sv;
		payload << "\"sha256\": \""sv;
		for (size_t i = 0; i < sizeof(app->app_elf_sha256); ++i) {
			payload << std::hex << std::setw(2) << std::setfill('0') << (int)app->app_elf_sha256[i];
		}
		payload << "\"}";

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void roomsStatus() {
		DBGLOGFD(log_, logFeature_, "roomsStatus\n");
		server_.enableCORS(true);

		ib::viewable_stringbuf payloadBuf;
		std::ostream payload(&payloadBuf);
		controller_.getRoomsStatus(payload);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void devicesFound() {
		DBGLOGFD(log_, logFeature_, "devicesFound\n");
		ib::viewable_stringbuf payloadBuf;
		std::ostream payload(&payloadBuf);
		controller_.getDevicesFound(payload);
		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void configWiFi() {
		DBGLOGFD(log_, logFeature_, "configWiFi METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				ib::viewable_stringbuf payloadBuf;
				std::ostream payload(&payloadBuf);
				getWiFiSSID(payload);
				server_.sendView(200, "application/json"sv, payloadBuf.view());
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					break;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					break;
				}

				File file = SPIFFS.open("/cfg/cfgwifi.json", FILE_WRITE);
				if (!file) {
					server_.send(500, "text/plain", "Internal server error. Can't save wifi settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();
				server_.send(201);
				break;
			}
		}

	}

	void configDevice() {
		DBGLOGFD(log_, logFeature_, "configDevice METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_READ);
				if (!file) {
					server_.send(404, "text/plain", "FileNotFound");
					return;
				}
				server_.streamFile(file, "application/json");
				file.close();
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					break;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					break;
				}

				File file = SPIFFS.open("/cfg/cfgnetwork.json", FILE_WRITE);
				if (!file) {
					DBGLOGFD(log_, logFeature_, "configDevice. Can't open config file for write.\n");
					server_.send(500, "text/plain", "Internal server error. Can't save device settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();
				server_.send(201);
				break;
			}
		}
	}

	void configHardware() {
		DBGLOGFD(log_, logFeature_, "configHardware METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				File file = SPIFFS.open("/cfg/cfgpins.json", FILE_READ);
				if (!file) {
					server_.send(404, "text/plain", "FileNotFound");
					return;
				}
				server_.streamFile(file, "application/json");
				file.close();
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					break;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					break;
				}

				File file = SPIFFS.open("/cfg/cfgpins.json", FILE_WRITE);
				if (!file) {
					DBGLOGFD(log_, logFeature_, "configHardware. Can't open config file for write.\n");
					server_.send(500, "text/plain", "Internal server error. Can't save hardware settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();
				server_.send(201);
				break;
			}
		}
	}

	void i2cScan() {
		DBGLOGFD(log_, logFeature_, "i2cScan\n");

		// Known I2C device address ranges (helpers for frontend labeling)
		struct KnownRange {
			uint8_t from;
			uint8_t to;
			const char *type;
		};
		static constexpr KnownRange ranges[] = {
			{0x20, 0x27, "pcf8574"},  // PCF8574 GPIO expanders
			{0x20, 0x27, "mcp23017"}, // MCP23017 GPIO expanders (same address range)
			{0x38, 0x3F, "pcf8574a"}, // PCF8574A variants
			{0x50, 0x57, "eeprom"},   // I2C EEPROMs (AT24Cx family)
			{0x68, 0x69, "rtc"},      // RTC modules (DS1307 / DS3231 common addresses)
		};

		ib::viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		ss << "[";
		bool first = true;

		for (uint8_t addr = 0x03; addr < 0x78; ++addr) {
			Wire.beginTransmission(addr);
			if (Wire.endTransmission() == 0) {
				const char *guessedType = "unknown";
				for (auto const &r : ranges) {
					if (addr >= r.from && addr <= r.to) {
						guessedType = r.type;
						break;
					}
				}
				if (!first)
					ss << ",";
				first = false;
				ss << "{\"address\":" << static_cast<int>(addr) << ",\"type\":\"" << guessedType << "\"}";
			}
		}
		ss << "]";

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void gpioTestStart() {
		DBGLOGFD(log_, logFeature_, "gpioTestStart\n");

		if (!server_.hasArg("plain")) {
			server_.send(400, "text/plain", "Missing body");
			return;
		}
		auto body = server_.arg("plain");
		if (body.isEmpty()) {
			server_.send(400, "text/plain", "Empty body");
			return;
		}

		std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(body.c_str()), &cJSON_Delete);
		if (!root) {
			server_.send(400, "text/plain", "Invalid JSON");
			return;
		}

		auto durationObj = cJSON_GetObjectItem(root.get(), "duration");
		if (!durationObj || !cJSON_IsNumber(durationObj) || durationObj->valueint <= 0 || durationObj->valueint > 3600) {
			server_.send(400, "text/plain", "Invalid duration (1-3600s)");
			return;
		}
		uint32_t duration = durationObj->valueint;

		auto boilerObj = cJSON_GetObjectItem(root.get(), "boiler");
		bool boilerState = boilerObj && cJSON_IsTrue(boilerObj);

		auto valvesArr = cJSON_GetObjectItem(root.get(), "valves");
		std::vector<bool> valveStates;
		if (valvesArr && cJSON_IsArray(valvesArr)) {
			int size = cJSON_GetArraySize(valvesArr);
			for (int i = 0; i < size; ++i) {
				auto item = cJSON_GetArrayItem(valvesArr, i);
				valveStates.push_back(cJSON_IsTrue(item));
			}
		}

		DBGLOGFD(log_, logFeature_, "gpioTestStart boiler: %d, valves: %zu, duration: %ds\n", boilerState, valveStates.size(), duration);
		controller_.startManualGpioTest(boilerState, valveStates, duration);
		server_.send(200, "text/plain", "OK");
	}

	void gpioTestStop() {
		DBGLOGFD(log_, logFeature_, "gpioTestStop\n");
		controller_.stopManualGpioTest();
		server_.send(200, "text/plain", "OK");
	}

	void configDebug() {
		DBGLOGFD(log_, logFeature_, "configDebug METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				std::unique_ptr<cJSON, decltype(&cJSON_Delete)> features(cJSON_CreateObject(), &cJSON_Delete);
				for (auto const &[id, name] : log_->getRegisteredFeatures()) {
					if (!cJSON_HasObjectItem(features.get(), name.c_str())) {
						cJSON_AddBoolToObject(features.get(), name.c_str(), log_->isFeatureEnabled(id));
					}
				}
				std::unique_ptr<char, decltype(&cJSON_free)> payload(cJSON_PrintUnformatted(features.get()), &cJSON_free);
				server_.send(200, "application/json", payload.get());
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					return;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					return;
				}

				config::setDebugOptionsFromJson(body.c_str());
				DBGLOGFD(log_, logFeature_, "configDebug. Flags set.\n");

				File file = SPIFFS.open("/cfg/cfgdebug.json", FILE_WRITE);
				if (!file) {
					DBGLOGFD(log_, logFeature_, "configDebug. Can't open config file for write.\n");
					server_.send(500, "text/plain", "Internal server error. Can't save hardware settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();

				DBGLOGFD(log_, logFeature_, "configDebug. Flags stored.\n");
				server_.send(201);
				break;
			}

		}
	}


	void configBoiler() {
		DBGLOGFD(log_, logFeature_, "configBoiler METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				File file = SPIFFS.open("/cfg/cfgboiler.json", FILE_READ);
				if (!file) {
					server_.send(404, "text/plain", "FileNotFound");
					return;
				}
				server_.streamFile(file, "application/json");
				file.close();
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					break;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					break;
				}

				File file = SPIFFS.open("/cfg/cfgboiler.json", FILE_WRITE);
				if (!file) {
					DBGLOGFD(log_, logFeature_, "configBoiler. Can't open config file for write.\n");
					server_.send(500, "text/plain", "Internal server error. Can't save boiler settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();
				server_.send(201);
				break;
			}
		}
	}

	void temporaryOverride() {
		if (!server_.hasArg("plain")) {
			server_.send(204);
			return;
		}
		auto body = server_.arg("plain");
		if (body.isEmpty()) {
			server_.send(204);
			return;
		}

		std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(body.c_str()), &cJSON_Delete);

		auto obj = cJSON_GetObjectItem(root.get(), "temperature");
		if (!obj || obj->type != cJSON_Number) {
			DBGLOGFD(log_, logFeature_, "temporaryOverride bad request: '%s'\n", body.c_str());
			server_.send(400, "text/html", "Bad request. Missing temperature.");
			return;
		}
		auto temperature = obj->valueint;

		obj = cJSON_GetObjectItem(root.get(), "validSeconds");
		if (!obj || obj->type != cJSON_Number) {
			DBGLOGFD(log_, logFeature_, "temporaryOverride bad request: '%s'\n", body.c_str());
			server_.send(400, "text/html", "Bad request. Missing time.");
			return;
		}
		auto validSeconds = obj->valueint;

		obj = cJSON_GetObjectItem(root.get(), "roomName");
		if (!obj || obj->type != cJSON_String) {
			DBGLOGFD(log_, logFeature_, "temporaryOverride bad request: '%s'\n", body.c_str());
			server_.send(400, "text/html", "Bad request. Missing room name.");
			return;
		}
		auto roomName = obj->valuestring;

		DBGLOGFD(log_, logFeature_, "temporaryOverride '%s' temp: %d secs: %d\n", roomName, temperature, validSeconds);

		if (!controller_.setRoomTemporaryTemperature(roomName, temperature, validSeconds)) {
			std::stringstream error;
			error << "Room " << roomName << " not found";
			std::string errorStr = error.str();
			server_.send(404, "text/plain", errorStr.c_str());
			return;
		}
		server_.send(201);
	}

	void configPrograms() {
		auto programName = server_.pathArg(0);
		if (programName.indexOf("..") >= 0 || programName.indexOf('/') >= 0 || programName.indexOf('\\') >= 0) {
			server_.send(400, "text/plain", "Invalid program name");
			return;
		}
		String filename = "/programs/" + programName + ".json";

		DBGLOGFD(log_, logFeature_, "configPrograms for '%s' METHOD %d\n", filename.c_str(), server_.method());

		switch (server_.method()) {
			case HTTP_GET: {
				if (!SPIFFS.exists(filename)) {
					server_.send(404, "text/plain", "Program " + server_.pathArg(0) + " not found");
					break;
				}

				DBGLOGFD(log_, logFeature_, "Reading config for '%s'\n", filename.c_str());
				File file = SPIFFS.open(filename, FILE_READ);
				server_.streamFile(file, "application/json");
				file.close();
				break;
			}
			case HTTP_POST: {
				if (!server_.hasArg("plain")) {
					server_.send(204);
					break;
				}
				auto body = server_.arg("plain");
				if (body.isEmpty()) {
					server_.send(204);
					break;
				}

				//TODO parse/validate json
				DBGLOGFD(log_, logFeature_, "Received program: '%s'\n", filename.c_str());
				File file = SPIFFS.open(filename, FILE_WRITE);
				if (!file) {
					server_.send(500, "text/plain", "Failed to open file for writing");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();

				auto currentProgram = config::getCurrentProgram();
				if (currentProgram == server_.pathArg(0).c_str()) {
					DBGLOGFD(log_, logFeature_, "Program reloaded: '%s'\n", filename.c_str());
					controller_.reloadConfiguration();
					server_.send(205);
					return;
				}
				server_.send(204);
				break;
			}
			case HTTP_DELETE: {
				if (!SPIFFS.exists(filename)) {
					server_.send(404, "text/plain", "Program " + server_.pathArg(0) + " not found");
					break;
				}
				if (SPIFFS.remove(filename)) {
					server_.send(204);
				} else {
					server_.send(500, "text/plain", "Internal server error during removing program " + server_.pathArg(0));
				}
				break;
			}
			case HTTP_OPTIONS:
				server_.sendHeader("Allow", "OPTIONS, GET, POST, DELETE");
				server_.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, DELETE");

				server_.send(204);
				break;
			default:
				server_.send(501, "text/plain", "Not implemented");
				break;
		}

	}

	void index() {
		serveFile("/index.html");
	}

	void serveFile(const char *serverPath) {
		DBGLOGFD(log_, logFeature_, "Request for: '%s'\n", serverPath);

		String path = "/html";
		path += serverPath;

		if (!SPIFFS.exists(path)) {
			path += ".gz";
		}

		if (!SPIFFS.exists(path)) {
			server_.send(404, "text/plain", "FileNotFound");
			return;
		}
		File file = SPIFFS.open(path, FILE_READ);
		if (!file) {
			server_.send(500, "text/plain", "Internal Server Error. Can't open file.");
			return;
		}

		auto content = getContentType(path);

		if (content.first) {
			server_.sendHeader("Cache-Control", "max-age=3600");
			server_.sendHeader("Cache-Control", "private");
		}

		server_.streamFile(file, content.second);
		file.close();
	}

	std::pair<bool, const char *>getContentType(String path) {
		if (path.endsWith("css")) {
			return {false, "text/css"};
		} else if (path.endsWith("css.gz")) {
			return {false, "text/css"};
		} else if (path.endsWith("js")) {
			return {false, "text/javascript"};
		} else if (path.endsWith("js.gz")) {
			return {false, "text/javascript"};
		} else if (path.endsWith("png")) {
			return {true, "image/png"};
		} else if (path.endsWith("jpg")) {
			return {true, "image/jpeg"};
		} else if (path.endsWith("html.gz")) {
			return {false, "text/html"};
		} else if (path.endsWith("html")) {
			return {false, "text/html"};
		} else {
			return {true, "text/plain"};
		}
	}

	void handleOTAUpdate() {
		HTTPUpload &upload = server_.upload();

		if (upload.status == UPLOAD_FILE_START) {
			auto size = server_.arg("size");
			long fileSize = atol(size.c_str());

			DBGLOGFD(log_, logFeature_, "handleOTA START '%s', totalSize: '%zu'\n", upload.filename.c_str(), fileSize);

			ota_ = OTAUpload{};

			ota_.partition = esp_ota_get_next_update_partition(NULL);
			if (!ota_.partition) {
				DBGLOGFD(log_, logFeature_, "OTA partition not found\n");
				ota_.errorMessage = "OTA partition not found"sv;
				ota_.error = -1;
				return;
			}

			if (fileSize > 0 && fileSize > ota_.partition->size) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Partition size %zu smaller than binary file %zu!\n", ota_.partition->size, fileSize);
				ota_.errorMessage = "Partition smaller than file"sv;
				ota_.error = -1;
				return;
			}


			DBGLOGFD(log_, logFeature_, "handleOTA Found partition '%s', size: %d, encrypted: %d\n", ota_.partition->label, ota_.partition->size, ota_.partition->encrypted);

			DBGLOGFD(log_, logFeature_, "Beginning OTA\n");

			if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "Beginning OTA: esp_ota_mark_app_valid_cancel_rollback failed\n");
			}

			ota_.error = esp_ota_begin(ota_.partition, OTA_SIZE_UNKNOWN, &ota_.handle);
			if (ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "Beginning OTA failed!\n");
				return;
			}
			ota_.started = true;

			DBGLOGFD(log_, logFeature_, "Beginning OTA handle: %d\n", ota_.handle);
		} else if (upload.status == UPLOAD_FILE_WRITE) {
			if (!ota_.started || ota_.error != ESP_OK) {
				if (!ota_.writeErrorReported) {
					DBGLOGFD(log_, logFeature_, "handleOTA writing skipped, OTA error: %d\n", ota_.error);
					ota_.writeErrorReported = true;
				}
				return;
			}

			DBGLOGFD(log_, logFeature_, "handleOTA writing to OTA handle %d, size: %zu\n", ota_.handle, upload.currentSize);

			ota_.error = esp_ota_write(ota_.handle, upload.buf, upload.currentSize);
			if (ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTA writing to OTA handle %d, size: %zu FAILED, error: %d\n", ota_.handle, upload.currentSize, ota_.error);
				esp_ota_abort(ota_.handle);
				return;
			}
		} else if (upload.status == UPLOAD_FILE_END) {
			if (!ota_.started || ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTA upload end, error: %d\n", ota_.error);
				return;
			}

			DBGLOGFD(log_, logFeature_, "handleOTA ending OTA\n");

			ota_.error = esp_ota_end(ota_.handle);
			if (ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTA finalizing OTA handle %d, FAILED, error: %d\n", ota_.handle, ota_.error);
				return;
			}

			DBGLOGFD(log_, logFeature_, "handleOTA setting boot partition\n");

			ota_.error = esp_ota_set_boot_partition(ota_.partition);
			if (ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTA setting boot partition FAILED, error: %d\n", ota_.error);
				return;
			}
			ota_.success = true;

		} else if (upload.status == UPLOAD_FILE_ABORTED) {
			if (!ota_.started) {
				DBGLOGFD(log_, logFeature_, "handleOTA upload aborted, OTA not started. Error: %d\n", ota_.error);
				return;
			}
			DBGLOGFD(log_, logFeature_, "handleOTA ABORTED\n");
			esp_ota_abort(ota_.handle);
		}
	}

	void handleOTAResponse() {
		auto ctype = "text/plain"sv;
		if (ota_.success) {
			server_.sendView(200, ctype, "success"sv);
		} else {
			ib::viewable_stringbuf payloadBuf;
			std::ostream ss(&payloadBuf);
			ss << "Failure: ";
			ss << ota_.errorMessage << " (" << ota_.error << ')';
			server_.sendView(500, ctype, payloadBuf.view());
		}
	}

	void handleOTAFFSUpdate() {
		HTTPUpload &upload = server_.upload();

		if (upload.status == UPLOAD_FILE_START) {
			auto size = server_.arg("size");
			long fileSize = atol(size.c_str());

			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate START '%s', totalSize: '%zu'\n", upload.filename.c_str(), fileSize);

			ota_ = OTAUpload{};
			ota_.partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
			if (!ota_.partition) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate partition not found\n");
				ota_.errorMessage = "FS partition not found"sv;
				ota_.error = -1;
				return;
			}

			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Found partition '%s', size: %d, encrypted: %d\n", ota_.partition->label, ota_.partition->size, ota_.partition->encrypted);

			if (fileSize > 0 && fileSize > ota_.partition->size) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Partition size %zu smaller than binary file %zu!\n", ota_.partition->size, fileSize);
				ota_.errorMessage = "FS partition smaller than file"sv;
				ota_.error = -1;
				return;
			}

			SPIFFS.end();

			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Erasing partition\n");
			ota_.error = esp_partition_erase_range(ota_.partition, 0, ota_.partition->size);
			if (ota_.error != ESP_OK) {
				ota_.errorMessage = "FS partition erase failure"sv;
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Failed to erase SPIFFS partition!\n");
				return;
			}

			ota_.started = true;
		} else if (upload.status == UPLOAD_FILE_WRITE) {
			if (!ota_.started || ota_.error != ESP_OK) {
				if (!ota_.writeErrorReported) {
					DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate writing skipped, OTA error: %d\n", ota_.error);
					ota_.writeErrorReported = true;
				}
				return;
			}
			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate writing SPIFFS offset: %zu, size: %zu\n", ota_.offset, upload.currentSize);

			ota_.error = esp_partition_write(ota_.partition, ota_.offset, upload.buf, upload.currentSize);
			if (ota_.error != ESP_OK) {
				ota_.errorMessage = "FS partition write error"sv;
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate Failed to write SPIFFS partition: %d\n", ota_.error);
				return;
			}
			ota_.offset += upload.currentSize;
		} else if (upload.status == UPLOAD_FILE_END) {
			if (!ota_.started || ota_.error != ESP_OK) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate upload end, error: %d\n", ota_.error);
				return;
			}
			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate finished\n");
			ota_.success = true;
		} else if (upload.status == UPLOAD_FILE_ABORTED) {
			if (!ota_.started) {
				DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate upload aborted. Not started. Error: %d\n", ota_.error);
				return;
			}
			DBGLOGFD(log_, logFeature_, "handleOTAFFSUpdate ABORTED\n");
		}
	}

	struct OTAUpload {
		bool started = false;
		bool success = false;
		const esp_partition_t *partition = nullptr;
		size_t offset = 0;
		esp_ota_handle_t handle = 0;
		esp_err_t error = ESP_OK;
		std::string errorMessage;
		bool writeErrorReported = false;
	} ota_;

	HeatingController &controller_;
	WebServerStringView server_;

};

}
