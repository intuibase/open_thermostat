#pragma once
#include "BoilerConfigValidation.h"
#include <SPIFFS.h>
#include <WebServer.h>
#include <memory>

namespace heating {
// The REST route and native filesystem tests execute this same handler.
template<class Server, class Filesystem>
void handleBoilerConfiguration(Server &server, Filesystem &filesystem) {

	switch (server.method()) {
		default:
		case HTTP_GET: {
			File file = filesystem.open("/cfg/cfgboiler.json", FILE_READ);
			if (!file) {
				server.send(404, "text/plain", "FileNotFound");
				return;
			}
			server.streamFile(file, "application/json");
			file.close();
			break;
		}
		case HTTP_POST: {
			if (!server.hasArg("plain")) {
				server.send(204);
				break;
			}
			auto body = server.arg("plain");
			if (body.isEmpty()) {
				server.send(204);
				break;
			}

			std::unique_ptr<cJSON, decltype(&cJSON_Delete)> parsed(cJSON_Parse(body.c_str()), &cJSON_Delete);
			config::BoilerConfig validated;
			if (!config::parseBoilerConfig(parsed.get(), validated)) {
				server.send(400, "text/plain", "Invalid boiler settings. EMS temperature control requires fixedSupplyTemperature (20-90 C) within the heating limits; boiler setting is supported only with plain ON/OFF.");
				return;
			}

			File file = filesystem.open("/cfg/cfgboiler.json", FILE_WRITE);
			if (!file) {
				server.send(500, "text/plain", "Internal server error. Can't save boiler settings.");
				break;
			}
			file.write((uint8_t *)body.c_str(), body.length());
			file.close();
			server.send(201);
			break;
		}
	}
}
}
