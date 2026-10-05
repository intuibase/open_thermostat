#pragma once

#include <SPIFFS.h>
#include <esp_partition.h>

#include <string>

namespace heating {

class Filesystem {
public:
	bool mount();
	bool isMounted() const { return mounted_; }
	bool hasDualPartitions() const { return dualPartitions_; }
	bool usedFallbackSlot() const { return usedFallbackSlot_; }
	const std::string &activeLabel() const { return activeLabel_; }

	const esp_partition_t *updatePartition() const;
	bool beginSinglePartitionUpdate();
	bool validateAndSelectUpdate(const esp_partition_t *partition, std::string &result);
	bool restoreActive();

private:
	const esp_partition_t *find(const char *label) const;
	bool mountLabel(const std::string &label);
	bool validateMountedFilesystem() const;
	bool saveActiveSlot(const std::string &label) const;

	bool mounted_ = false;
	bool dualPartitions_ = false;
	bool usedFallbackSlot_ = false;
	std::string activeLabel_;
};

extern Filesystem filesystem;

}
