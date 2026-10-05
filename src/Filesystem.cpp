#include "Filesystem.h"

#include <Preferences.h>

namespace {
constexpr auto preferencesNamespace = "ot_fs";
constexpr auto activeSlotKey = "active_slot";
constexpr auto legacyLabel = "spiffs";
constexpr auto firstLabel = "spiffs0";
constexpr auto secondLabel = "spiffs1";
}

namespace heating {

Filesystem filesystem;

const esp_partition_t *Filesystem::find(const char *label) const {
	return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, label);
}

bool Filesystem::mountLabel(const std::string &label) {
	mounted_ = SPIFFS.begin(false, "/spiffs", 10, label.c_str());
	return mounted_;
}

bool Filesystem::mount() {
	usedFallbackSlot_ = false;
	auto first = find(firstLabel);
	auto second = find(secondLabel);
	dualPartitions_ = first != nullptr && second != nullptr && first->size == second->size;

	if (!dualPartitions_) {
		activeLabel_ = legacyLabel;
		return mountLabel(activeLabel_);
	}

	Preferences preferences;
	uint8_t slot = 0;
	if (preferences.begin(preferencesNamespace, true)) {
		slot = preferences.getUChar(activeSlotKey, 0) == 1 ? 1 : 0;
		preferences.end();
	}

	activeLabel_ = slot == 0 ? firstLabel : secondLabel;
	if (mountLabel(activeLabel_)) {
		return true;
	}

	activeLabel_ = slot == 0 ? secondLabel : firstLabel;
	if (!mountLabel(activeLabel_)) {
		return false;
	}

	// Recover automatically when the selected slot cannot be mounted but the
	// other slot is still valid.
	usedFallbackSlot_ = true;
	saveActiveSlot(activeLabel_);
	return true;
}

const esp_partition_t *Filesystem::updatePartition() const {
	if (!dualPartitions_) {
		auto partition = find(activeLabel_.c_str());
		return partition ? partition : esp_partition_find_first(
			ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
	}
	return find(activeLabel_ == firstLabel ? secondLabel : firstLabel);
}

bool Filesystem::beginSinglePartitionUpdate() {
	if (dualPartitions_) {
		return true;
	}
	if (mounted_) {
		SPIFFS.end();
		mounted_ = false;
	}
	return true;
}

bool Filesystem::validateMountedFilesystem() const {
	return SPIFFS.exists("/html/index.html") &&
		SPIFFS.exists("/html/update.html") &&
		SPIFFS.exists("/cfg/cfgnetwork.json");
}

bool Filesystem::saveActiveSlot(const std::string &label) const {
	if (!dualPartitions_) {
		return true;
	}
	Preferences preferences;
	if (!preferences.begin(preferencesNamespace, false)) {
		return false;
	}
	auto written = preferences.putUChar(activeSlotKey, label == secondLabel ? 1 : 0);
	preferences.end();
	return written == sizeof(uint8_t);
}

bool Filesystem::restoreActive() {
	if (mounted_) {
		SPIFFS.end();
		mounted_ = false;
	}
	return mountLabel(activeLabel_);
}

bool Filesystem::validateAndSelectUpdate(const esp_partition_t *partition, std::string &result) {
	if (!partition) {
		result = "update partition is missing";
		return false;
	}

	const std::string updateLabel = partition->label;
	if (mounted_) {
		SPIFFS.end();
		mounted_ = false;
	}

	bool updateMounted = mountLabel(updateLabel);
	bool updateValid = updateMounted && validateMountedFilesystem();
	if (mounted_) {
		SPIFFS.end();
		mounted_ = false;
	}

	if (!dualPartitions_) {
		activeLabel_ = updateLabel;
		mounted_ = mountLabel(activeLabel_);
		if (!updateMounted) result = "uploaded filesystem could not be mounted";
		else if (!updateValid) result = "uploaded filesystem is missing required files";
		else if (!mounted_) result = "verified filesystem could not be remounted";
		else result = "uploaded filesystem mounted and required files verified";
		return updateValid && mounted_;
	}

	bool activeRestored = mountLabel(activeLabel_);
	if (!updateValid) {
		result = updateMounted ? "uploaded slot is missing required files" : "uploaded slot could not be mounted";
		return false;
	}
	if (!saveActiveSlot(updateLabel)) {
		result = "uploaded slot verified, but selecting it in NVS failed";
		return false;
	}
	if (activeRestored) {
		result = "uploaded slot mounted and verified; current slot restored; new slot selected for next boot";
		return true;
	}

	// Recovery mode can start with both slots unmountable. In that case keep
	// the newly verified slot mounted and use it immediately as well as after
	// the next reboot.
	activeLabel_ = updateLabel;
	mounted_ = mountLabel(activeLabel_);
	result = mounted_
		? "uploaded slot mounted and verified; previous slot failed to remount; new slot activated immediately"
		: "uploaded slot verified and selected, but neither slot could be remounted";
	return mounted_;
}

}
