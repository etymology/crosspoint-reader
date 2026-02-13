#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <iostream>

namespace serialization {
namespace {
// Guard against corrupted lengths in serialized files causing huge allocations.
constexpr uint32_t MAX_SERIALIZED_STRING_LENGTH = 256 * 1024;
}

template <typename T>
static void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
static void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
static void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

static void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

static void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

static void readString(std::istream& is, std::string& s) {
  uint32_t len = 0;
  is.read(reinterpret_cast<char*>(&len), sizeof(len));
  if (!is) {
    s.clear();
    return;
  }
  if (len > MAX_SERIALIZED_STRING_LENGTH) {
    s.clear();
    is.setstate(std::ios::failbit);
    return;
  }
  try {
    s.resize(len);
  } catch (...) {
    s.clear();
    is.setstate(std::ios::failbit);
    return;
  }
  if (len == 0) {
    return;
  }
  is.read(&s[0], len);
  if (!is) {
    s.clear();
  }
}

static void readString(FsFile& file, std::string& s) {
  uint32_t len = 0;
  if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) {
    s.clear();
    return;
  }
  if (len > MAX_SERIALIZED_STRING_LENGTH) {
    s.clear();
    return;
  }
  const int available = file.available();
  if (available < 0 || len > static_cast<uint32_t>(available)) {
    s.clear();
    return;
  }
  try {
    s.resize(len);
  } catch (...) {
    s.clear();
    return;
  }
  if (len == 0) {
    return;
  }
  if (file.read(&s[0], len) != static_cast<int>(len)) {
    s.clear();
  }
}
}  // namespace serialization
