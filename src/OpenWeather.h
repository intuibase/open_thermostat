#pragma once

#include "config.h"
#include "Logging.h"
#include <HTTPClient.h>
#include <cJSON.h>
#include <memory>
#include <mutex>

namespace heating {

namespace openweather {

static void fetchTask(void *pvParameters);
}

class OpenWeather {
public:
	ib::logger::LoggerInterface::LogFeatureType getLogFeature() const { return logFeature_; }
	std::shared_ptr<ib::logger::LoggerInterface> getLogger() const { return log_; }
	OpenWeather(std::shared_ptr<ib::logger::LoggerInterface> log, config::OpenWeatherConfig config) : log_(std::move(log)), config_(std::move(config)), query_(buildQuery()), lastFetch_(0) {
		if (log_) {
			static const auto id = [this] {
				auto feature = log_->addFeature("OpenWeather");
				log_->enableFeature(feature, false);
				return feature;
			}();
			logFeature_ = id;
		}
		DBGLOGFD(log_, logFeature_, "Configuration. Enabled: %d appid:%s lat:%s lon:%s interval: %d\n", config_.enabled, config_.appid.c_str(), config_.latitude.c_str(), config_.longitude.c_str(), config_.interval);
	}

	void operate() {
		if (!config_.enabled) {
			return;
		}

		auto now = millis();
		if (lastFetch_ > 0 && now < lastFetch_ + config_.interval * 1000) {
			unsigned long timeToWait = static_cast<unsigned long>(config_.interval * 1000);
			DBGLOGFD(log_, logFeature_, "Interval: %ld\n", timeToWait);
			timeToWait = timeToWait - (now - lastFetch_);
			DBGLOGFD(log_, logFeature_, "Waiting for interval (%ds) last: %ld, now: %ld, still need to wait for: %ld ms \n", config_.interval, lastFetch_, now, timeToWait);
			return;
		}

		DBGLOGFD(log_, logFeature_, "Requesting temperature, last: %ld, now: %ld\n", lastFetch_, now);
		lastFetch_ = now;

		xTaskCreate(heating::openweather::fetchTask, "OWTask", 2500, this, 1, NULL);
	}

	std::string getStatus() const {
		std::lock_guard<std::mutex> lock(mutex_);

		if (payload_.isEmpty()) {
			return "{}";
		}
		return {payload_.c_str(), payload_.length()};
	}

	struct Outdoor {
		bool valid = false;
		int16_t temp = 0;
		uint8_t humidity = 0;
		uint16_t pressure = 0;
		uint64_t date = 0;
	};

	std::optional<int16_t> getTemperature() {
		std::unique_lock<std::mutex> lock(mutex_);
		if (!outdoor_.valid) {
			return {};
		}
		return {outdoor_.temp};
	}

private:
	std::shared_ptr<ib::logger::LoggerInterface> log_;
	ib::logger::LoggerInterface::LogFeatureType logFeature_{};
	void parsePayload(String const &payload) {
		outdoor_.valid = false;
		std::unique_ptr<cJSON, decltype(&cJSON_Delete)> data(cJSON_Parse(payload.c_str()), &cJSON_Delete);

		auto main = cJSON_GetObjectItem(data.get(), "main");
		if (!main || main->type != cJSON_Object) {
			DBGLOGFD(log_, logFeature_, "Error, missing main\n", nullptr);
			return;
		}

		auto temp = json::getFloat(main, "temp"); // example 6.34 C

		outdoor_.temp = static_cast<int16_t>(temp * 100); // example 634
		outdoor_.pressure = static_cast<int16_t>(json::getInt(main, "pressure"));
		outdoor_.humidity = static_cast<uint8_t>(json::getInt(main, "humidity"));
		outdoor_.date = json::getInt(data.get(), "dt");
		outdoor_.valid = true;
	}

	void getData() {
		std::unique_lock<std::mutex> lock(mutex_);
		if (getDataInProgress_) {
			DBGLOGFD(log_, logFeature_, "getData already in progress\n");
			return;
		}
		getDataInProgress_ = true;
		lock.unlock();

		http_.begin(query_.c_str());
		int httpResponseCode = http_.GET();
		if (httpResponseCode > 0) {
			auto payload = http_.getString();

			lock.lock();
			payload_ = std::move(payload);
			DBGLOGFD(log_, logFeature_, "Content size: %d. Response: %d\n", http_.getSize(), httpResponseCode);
			if (payload_.length() > 0) {
				parsePayload(payload_);
				DBGLOGFD(log_, logFeature_, "Date: %ld Temp: %f, Humidity: %d%% Pressure: %d hPa\n", static_cast<unsigned long>(outdoor_.date), static_cast<float>(outdoor_.temp) / 100, static_cast<int>(outdoor_.humidity), static_cast<int>(outdoor_.pressure));
			}
			lock.unlock();

		}
		else {
			DBGLOGFD(log_, logFeature_, "Error code %d\n", httpResponseCode);
		}
		http_.end();
		lock.lock();
		getDataInProgress_ = false;
	}

	std::string buildQuery() {
		return "http://api.openweathermap.org/data/2.5/weather?lat=" + config_.latitude + "&lon=" + config_.longitude + "&appid=" + config_.appid + "&units=metric";
	}

	HTTPClient http_;
	config::OpenWeatherConfig config_;
	std::string query_;
	unsigned long lastFetch_;
	Outdoor outdoor_;
	String payload_;
	mutable std::mutex mutex_;
	bool getDataInProgress_ = false;
	friend void heating::openweather::fetchTask(void *pvParameters);


};

namespace openweather {
static void fetchTask(void *pvParameters) {
	auto ow = reinterpret_cast<OpenWeather *>(pvParameters);
	ow->getData();
	DBGLOGFD(ow->getLogger(), ow->getLogFeature(), "Free memory %d/%d (minimum was: %d) MaxAlloc: %d MinPeekStack: %d UpTime: %lds\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), uxTaskGetStackHighWaterMark(nullptr), millis()/1000);
	vTaskDelete(NULL);
}
}

}
