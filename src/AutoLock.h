#pragma once

#include <freertos/FreeRTOS.h>

#include "Logger.h"

class AutoLock {
public:
	AutoLock(SemaphoreHandle_t semaphore, TickType_t ticks = 100) : semaphore_(semaphore) {
		DBGLOGM(debug::debug.debugAutoLock, "AutoLock", "acquire %p ticks=%d\n", semaphore_, ticks);

		while ((xSemaphoreTake(semaphore_, ticks) != pdTRUE)) {
			DBGLOGM(debug::debug.debugAutoLock, "AutoLock","can't obtain %p\n", semaphore_);
		}
		//		Serial.println("AutoLock OK");
	}
	~AutoLock() {
		xSemaphoreGive(semaphore_);
		DBGLOGM(debug::debug.debugAutoLock, "AutoLock", "release %p\n", semaphore_);
	}
private:
	SemaphoreHandle_t semaphore_;
};
