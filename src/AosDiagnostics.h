#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <type_traits>

namespace heating::aos {
// Includes the logger timestamp/feature prefix. The TCP sink must accept whole records.
inline constexpr size_t diagnosticMaxRecordBytes = 4096;

// Observation only: none of these fields participate in heating decisions.
struct HeatContext {
	std::optional<bool> boilerCommanded, valveOpen, emsHeating;
	std::string program;
	bool manualTest = false;
	bool usesEms = false;
	const char *outdoorSource = "unknown";
	const char *invalidReason = "untrusted_heat";
};

class DiagnosticCadence {
public:
	bool due(int64_t now) const { return !last_ || now - *last_ >= 60; }
	void mark(int64_t now) { last_ = now; }

private:
	std::optional<int64_t> last_;
};

// One flat JSON object per log line, including correct escaping of room names
// and null for missing/nonfinite measurements. This is also the CSV schema.
class DiagnosticJson {
public:
	explicit DiagnosticJson(std::ostream &out) : out_(out) { out_ << '{'; }
	void text(const char *key, const std::string &value) {
		field(key);
		quoted(value);
	}
	void boolean(const char *key, bool value) {
		field(key);
		out_ << (value ? "true" : "false");
	}
	void boolean(const char *key, std::optional<bool> value) {
		if (value)
			boolean(key, *value);
		else
			null(key);
	}
	template <class T>
	void number(const char *key, T value) {
		field(key);
		if constexpr (std::is_floating_point_v<T>) {
			if (!std::isfinite(value)) {
				out_ << "null";
				return;
			}
		}
		out_ << value;
	}
	template <class T>
	void optionalNumber(const char *key, std::optional<T> value) {
		if (value)
			number(key, *value);
		else
			null(key);
	}
	void null(const char *key) {
		field(key);
		out_ << "null";
	}
	void finish() { out_ << '}'; }

private:
	std::ostream &out_;
	bool first_ = true;
	void field(const char *key) {
		if (!first_)
			out_ << ',';
		first_ = false;
		quoted(key);
		out_ << ':';
	}
	void quoted(const std::string &value) {
		out_ << '"';
		for (unsigned char c : value) {
			switch (c) {
				case '"':
					out_ << "\\\"";
					break;
				case '\\':
					out_ << "\\\\";
					break;
				case '\n':
					out_ << "\\n";
					break;
				case '\r':
					out_ << "\\r";
					break;
				case '\t':
					out_ << "\\t";
					break;
				default:
					if (c < 0x20)
						out_ << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
					else
						out_ << static_cast<char>(c);
			}
		}
		out_ << '"';
	}
};
} // namespace heating::aos
