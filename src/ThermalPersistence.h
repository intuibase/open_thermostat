#pragma once
#include "ThermalModel.h"
#include <array>
#include <cstring>
#include <cstddef>
#include <string>
#ifdef ARDUINO
#include <Preferences.h>
#endif

namespace heating::aos {
// Versioned, sensor-bound snapshot; raw history is deliberately never persisted.
struct Snapshot {
	uint32_t version = 2;
	std::array<uint8_t, 6> sensor{};
	uint8_t reserved[2]{};
	ModelState state;
	uint32_t checksum = 0;
};
struct LegacySnapshot {
	uint32_t version = 1;
	std::array<uint8_t, 6> sensor{};
	uint8_t reserved[2]{};
	std::array<uint8_t, offsetof(ModelState, warmupRate)> state{};
	uint32_t checksum = 0;
};
static_assert(offsetof(ModelState, warmupRate) == 60, "Keep the version-1 parameter prefix unchanged");
static_assert(sizeof(LegacySnapshot) == 76, "Keep the version-1 NVS layout unchanged");
template <class T> inline uint32_t checksum(const T &s) {
	const auto *p = reinterpret_cast<const uint8_t *>(&s);
	uint32_t value = 2166136261u;
	for (size_t i = 0; i < offsetof(T, checksum); ++i)
		value = (value ^ p[i]) * 16777619u;
	return value;
}
inline bool validSnapshot(const Snapshot &s, const std::array<uint8_t, 6> &sensor) {
	return s.version == 2 && s.sensor == sensor && s.checksum == checksum(s) && validState(s.state);
}
inline bool migrateSnapshot(const LegacySnapshot &s, const std::array<uint8_t, 6> &sensor, ModelState &state) {
	if (s.version != 1 || s.sensor != sensor || s.checksum != checksum(s)) return false;
	ModelState migrated;
	std::memcpy(&migrated, s.state.data(), s.state.size());
	if (!validState(migrated)) return false;
	state = migrated;
	return true;
}
inline bool loadModel(const std::string &id, const std::array<uint8_t, 6> &sensor, ModelState &state) {
#ifdef ARDUINO
	Preferences prefs;
	if (id.empty() || !prefs.begin("thermal-aos", true))
		return false;
	bool loaded = false;
	const size_t length = prefs.getBytesLength(id.c_str());
	if (length == sizeof(Snapshot)) {
		Snapshot snapshot{};
		loaded = prefs.getBytes(id.c_str(), &snapshot, sizeof(snapshot)) == sizeof(snapshot) && validSnapshot(snapshot, sensor);
		if (loaded) state = snapshot.state;
	} else if (length == sizeof(LegacySnapshot)) {
		LegacySnapshot snapshot{};
		loaded = prefs.getBytes(id.c_str(), &snapshot, sizeof(snapshot)) == sizeof(snapshot) && migrateSnapshot(snapshot, sensor, state);
	}
	prefs.end();
	return loaded;
#else
	return false;
#endif
}
inline bool saveModel(const std::string &id, const std::array<uint8_t, 6> &sensor, const ModelState &state) {
#ifdef ARDUINO
	if (!validState(state) || id.empty())
		return false;
	Snapshot snapshot{};
	snapshot.sensor = sensor;
	snapshot.state = state;
	snapshot.checksum = checksum(snapshot);
	Preferences prefs;
	if (!prefs.begin("thermal-aos", false))
		return false;
	const bool saved = prefs.putBytes(id.c_str(), &snapshot, sizeof(snapshot)) == sizeof(snapshot);
	prefs.end();
	return saved;
#else
	return false;
#endif
}
inline bool eraseModel(const std::string &id) {
#ifdef ARDUINO
	Preferences prefs;
	if (id.empty() || !prefs.begin("thermal-aos", false))
		return false;
	const bool erased = !prefs.isKey(id.c_str()) || prefs.remove(id.c_str());
	prefs.end();
	return erased;
#else
	return true;
#endif
}
} // namespace heating::aos
