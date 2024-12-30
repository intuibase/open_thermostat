#pragma once


#include <SPIFFS.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <cJSON.h>

#include "ViewableStringBuf.h"
#include "HeatingController.h"
#include "Logger.h"

#include "Network.h"
#include <string_view>

namespace heating {

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

};

class REST {
public:
	REST(HeatingController &controller, uint16_t listenPort) : controller_(controller), server_(listenPort) {
		server_.enableCORS(true);
		server_.enableCrossOrigin(true);

		server_.on("/status/wifi", [this]() {
			DBGLOGREST("REST:wifiNetworks\n");

			ViewableStringBuf payloadBuf;
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
		server_.on("/params/boiler", [this]() { emsParams(); });


		server_.on("/config/temporary", HTTP_POST, [this]() { temporaryOverride(); }); // GET/POST/DELETE name without .json, case sensitive

		server_.on(UriBraces("/config/programs/{}"), [this]() { configPrograms(); }); // GET/POST/DELETE name without .json, case sensitive
		server_.on("/config/wifi", [this]() { configWiFi(); });
		server_.on("/config/device", [this]() { configDevice(); });
		server_.on("/config/boiler", [this]() { configBoiler(); });
		server_.on("/config/hardware", [this]() { configHardware(); });
		server_.on("/config/debug", [this]() { configDebug(); });

		server_.on("/config/program/current", [this]() { configProgramCurrent(); }); // get selected program
		server_.on("/config/reboot", HTTP_GET, [this]() { configReboot(); });

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
	// bool auth() {
	// 	if (!server_.authenticate("admin", "admin")) {
	// 		server_.requestAuthentication(DIGEST_AUTH);
	// 		return false;
	// 	}
	// 	return true;
	// }

	void showPrograms() {
		DBGLOGREST("showPrograms\n");

		ViewableStringBuf payloadBuf;
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
			DBGLOGREST("showPrograms '%s'\n", file.name());

			if (file.isDirectory()) {
				DBGLOGREST("showPrograms skip directory: %s\n", file.name());
				continue;
			}

			std::string name = file.name();
			auto pos = name.find(jsonExtension);
			if (pos == std::string::npos) {
				DBGLOGREST("showPrograms skip non-json: %s\n", file.name());
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
		DBGLOGREST("configProgramCurrent method: %d\n", server_.method());

		switch (server_.method()) {
			case HTTP_GET: {
				File file = SPIFFS.open("/cfg/cfgprogram.json", FILE_READ);
				if (!file) {
					DBGLOGREST("configProgramCurrent missing config");
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
					DBGLOGREST("configProgramCurrent error parsing program\n");
					server_.send(400, "text/html", "Program parsing failure. Config not modified");
					return;
				}

				std::string filename = "/programs/" + program + ".json";

				if (!SPIFFS.exists(filename.c_str())) {
					DBGLOGREST("Received new program configuration: '%s'. Program does not exists!\n", program.c_str());
					server_.send(400, "text/html", "Program does not exists. Config not modified");
					return;
				}

				DBGLOGREST("Received new program configuration: '%s'\n", program.c_str());
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
		DBGLOGREST("configReboot\n");
		server_.send(200);
		server_.stop();
		ESP.restart();
	}

	void boilerStatus() {
		DBGLOGREST("boilerStatus\n");

		ViewableStringBuf payloadBuf;
		std::ostream payload(&payloadBuf);

		controller_.getBoilerStatus(payload);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void emsStatus() {
		DBGLOGREST("emsStatus\n");

		ViewableStringBuf payloadBuf;
		std::ostream ss(&payloadBuf);
		controller_.getEMSStatus(ss);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void emsParams() {
		DBGLOGREST("emsParams\n");
		ViewableStringBuf payloadBuf;
		std::ostream payload(&payloadBuf);
		controller_.getEMSBoilerParams(payload);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void status() {
		DBGLOGREST("status\n");
		server_.enableCORS(true);
		ViewableStringBuf payloadBuf;
		std::ostream payload(&payloadBuf);
		controller_.getFullStatus(payload);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void roomsStatus() {
		DBGLOGREST("roomsStatus\n");
		server_.enableCORS(true);

		ViewableStringBuf payloadBuf;
		std::ostream payload(&payloadBuf);
		controller_.getRoomsStatus(payload);

		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void devicesFound() {
		DBGLOGREST("devicesFound\n");
		ViewableStringBuf payloadBuf;
		std::ostream payload(&payloadBuf);
		controller_.getDevicesFound(payload);
		server_.sendView(200, "application/json"sv, payloadBuf.view());
	}

	void configWiFi() {
		DBGLOGREST("configWiFi METHOD %d\n", server_.method());

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				ViewableStringBuf payloadBuf;
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
		DBGLOGREST("configDevice METHOD %d\n", server_.method());

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
					DBGLOGREST("configDevice. Can't open config file for write.\n");
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
		DBGLOGREST("configHardware METHOD %d\n", server_.method());

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
					DBGLOGREST("configHardware. Can't open config file for write.\n");
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

	void configDebug() {
		DBGLOGREST("configDebug METHOD %d\n", server_.method());

		#define DEBUG_OPTION_TO_STREAM(name) "\""#name"\": " << (debug::debug.name ? "true" : "false")

		switch (server_.method()) {
			default:
			case HTTP_GET: {
				ViewableStringBuf payloadBuf;
				std::ostream ss(&payloadBuf);
				ss << "{";
				ss << DEBUG_OPTION_TO_STREAM(debugRoomTemperatures) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugAutoLock) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugHeatingController) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugTemperatureReader) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugREST) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugOpenWeather) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugBoilerController) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugEmsBusUart) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugEmsBusUartForwarder) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugEmsController) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugEmsVerbose) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugMQTT) << ",";
				ss << DEBUG_OPTION_TO_STREAM(debugFatal);
				ss << "}";
				server_.sendView(200, "application/json"sv, payloadBuf.view());
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
				DBGLOGREST("configDebug. Flags set.\n");

				File file = SPIFFS.open("/cfg/cfgdebug.json", FILE_WRITE);
				if (!file) {
					DBGLOGREST("configDebug. Can't open config file for write.\n");
					server_.send(500, "text/plain", "Internal server error. Can't save hardware settings.");
					break;
				}
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();

				DBGLOGREST("configDebug. Flags stored.\n");
				server_.send(201);
				break;
			}

		}
	}


	void configBoiler() {
		DBGLOGREST("configBoiler METHOD %d\n", server_.method());

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
					DBGLOGREST("configBoiler. Can't open config file for write.\n");
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
			DBGLOGREST("temporaryOverride bad request: '%s'\n", body.c_str())
			server_.send(400, "text/html", "Bad request. Missing temperature.");
			return;
		}
		auto temperature = obj->valueint;

		obj = cJSON_GetObjectItem(root.get(), "validSeconds");
		if (!obj || obj->type != cJSON_Number) {
			DBGLOGREST("temporaryOverride bad request: '%s'\n", body.c_str())
			server_.send(400, "text/html", "Bad request. Missing time.");
			return;
		}
		auto validSeconds = obj->valueint;

		obj = cJSON_GetObjectItem(root.get(), "roomName");
		if (!obj || obj->type != cJSON_String) {
			DBGLOGREST("temporaryOverride bad request: '%s'\n", body.c_str())
			server_.send(400, "text/html", "Bad request. Missing room name.");
			return;
		}
		auto roomName = obj->valuestring;

		DBGLOGREST("temporaryOverride '%s' temp: %d secs: %d\n", roomName, temperature, validSeconds);

		if (!controller_.setRoomTemporaryTemperature(roomName, temperature, validSeconds)) {
			std::stringstream error;
			error << "Room " << roomName << " not found";
			std::string errorStr = error.str();
			server_.send(404, "text/plain", errorStr.c_str());
		}
		server_.send(201);
	}

	void configPrograms() {
		String filename = "/programs/" + server_.pathArg(0) + ".json";

		DBGLOGREST("configPrograms for '%s' METHOD %d\n", filename.c_str(), server_.method());

		switch (server_.method()) {
			case HTTP_GET: {
				if (!SPIFFS.exists(filename)) {
					server_.send(404, "text/plain", "Program " + server_.pathArg(0) + " not found");
					break;
				}

				DBGLOGREST("Reading config for '%s'\n", filename.c_str());
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
				DBGLOGREST("Received program: '%s'\n", filename.c_str());
				File file = SPIFFS.open(filename, FILE_WRITE);
				file.write((uint8_t *)body.c_str(), body.length());
				file.close();

				auto currentProgram = config::getCurrentProgram();
				if (currentProgram == server_.pathArg(0).c_str()) {
					DBGLOGREST("Program reloaded: '%s'\n", filename.c_str());
					controller_.reloadConfiguration();
					server_.send(205);
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
		DBGLOGREST("Request for: '%s'\n", serverPath);

		String path = "/html";
		path += serverPath;

		if (!SPIFFS.exists(path)) {
			path += ".gz";
		}

		if (!SPIFFS.exists(path)) {
			server_.send(404, "text/plain", "FileNotFound");
		}
		File file = SPIFFS.open(path, FILE_READ);
		if (!file) {
			server_.send(500, "text/plain", "Internal Server Error. Can't open file.");
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

	HeatingController &controller_;
	WebServerStringView server_;

};

}
