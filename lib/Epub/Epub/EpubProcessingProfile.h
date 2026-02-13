#pragma once

#include <cstddef>
#include <cstdint>

struct EpubProcessingProfile {
  size_t sectionStreamBufferSize = 4096;
  size_t htmlParseChunkSize = 4096;
  uint16_t pageProcessLogInterval = 25;
  bool cacheLineMetrics = true;

  static constexpr size_t DEFAULT_CHUNK_SIZE = 1024;

  static EpubProcessingProfile optimized() { return EpubProcessingProfile{}; }

  static EpubProcessingProfile baseline() {
    EpubProcessingProfile profile;
    profile.sectionStreamBufferSize = 1024;
    profile.htmlParseChunkSize = 1024;
    profile.pageProcessLogInterval = 1;
    profile.cacheLineMetrics = false;
    return profile;
  }

  [[nodiscard]] constexpr size_t sectionChunkSizeOrDefault() const {
    return sectionStreamBufferSize > 0 ? sectionStreamBufferSize : DEFAULT_CHUNK_SIZE;
  }

  [[nodiscard]] constexpr size_t parseChunkSizeOrDefault() const {
    return htmlParseChunkSize > 0 ? htmlParseChunkSize : DEFAULT_CHUNK_SIZE;
  }
};

