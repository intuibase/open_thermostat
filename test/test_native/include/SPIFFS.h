#pragma once
#include <Arduino.h>
#include <filesystem>
#include <fstream>
#include <memory>
#define FILE_READ "r"
#define FILE_WRITE "w"

// Real files beneath a test-owned temporary root; no device or private config.
class File {
 std::shared_ptr<std::fstream> stream_;
public:
 File() = default;
 File(const std::filesystem::path &path, bool write) : stream_(std::make_shared<std::fstream>(path,
  write ? std::ios::out | std::ios::trunc | std::ios::binary : std::ios::in | std::ios::binary)) {}
 explicit operator bool() const { return stream_ && stream_->is_open(); }
 String readString() { return std::string(std::istreambuf_iterator<char>(*stream_), {}); }
 size_t write(uint8_t *data,size_t length) { stream_->write(reinterpret_cast<char *>(data),length); stream_->flush(); return *stream_ ? length : 0; }
 void close() { if (stream_) stream_->close(); }
};
class NativeFilesystem {
public:
 std::filesystem::path root;
 bool failOpen = false;
 unsigned writes = 0;
 File open(const char *path,const char *mode) {
  if (failOpen) return {};
  const bool write=std::string(mode)==FILE_WRITE;
  if (write) ++writes;
  return {root / std::filesystem::path(path).relative_path(),write};
 }
};
