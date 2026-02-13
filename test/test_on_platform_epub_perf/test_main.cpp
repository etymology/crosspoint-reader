#include <Arduino.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <builtinFonts/all.h>
#include <unity.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "Epub/EpubProcessingProfile.h"
#include "Epub/Section.h"
#include "Epub/css/CssStyle.h"

#ifndef EPUB_PERF_TEST_BOOK_PATH
#define EPUB_PERF_TEST_BOOK_PATH "/books/perf_large.epub"
#endif

namespace {

constexpr int PERF_FONT_ID = 1233852315;  // BOOKERLY_14_FONT_ID
constexpr float PERF_LINE_COMPRESSION = 1.0f;
constexpr bool PERF_EXTRA_PARAGRAPH_SPACING = true;
constexpr uint8_t PERF_PARAGRAPH_ALIGNMENT = static_cast<uint8_t>(CssTextAlign::Justify);
constexpr uint16_t PERF_VIEWPORT_WIDTH = 480;
constexpr uint16_t PERF_VIEWPORT_HEIGHT = 800;
constexpr bool PERF_HYPHENATION_ENABLED = true;
constexpr bool PERF_EMBEDDED_STYLE_ENABLED = true;
constexpr uint8_t PERF_TRIALS = 3;
constexpr const char* PERF_CACHE_DIR = "/.crosspoint_perf";
constexpr uint32_t PERF_ALLOWED_SLOWDOWN_PERCENT = 10;

struct ProfileRunResult {
  bool success = false;
  uint16_t pageCount = 0;
  std::vector<uint32_t> trialTimesMs;
  uint32_t medianMs = 0;
};

HalDisplay display;
HalGPIO gpio;
GfxRenderer renderer(display);
bool runtimeInitialized = false;

EpdFont bookerly14RegularFont(&bookerly_14_regular);
EpdFont bookerly14BoldFont(&bookerly_14_bold);
EpdFont bookerly14ItalicFont(&bookerly_14_italic);
EpdFont bookerly14BoldItalicFont(&bookerly_14_bolditalic);
EpdFontFamily bookerly14FontFamily(&bookerly14RegularFont, &bookerly14BoldFont, &bookerly14ItalicFont,
                                   &bookerly14BoldItalicFont);

uint32_t median(std::vector<uint32_t> values) {
  if (values.empty()) {
    return 0;
  }

  std::sort(values.begin(), values.end());
  const size_t mid = values.size() / 2;
  if ((values.size() % 2) == 0) {
    return static_cast<uint32_t>((values[mid - 1] + values[mid]) / 2);
  }
  return values[mid];
}

bool initRuntime() {
  if (runtimeInitialized) {
    return true;
  }

  // Mirror production startup ordering: configure GPIO/SPI before SD init.
  gpio.begin();

  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && (millis() - start) < 2000) {
    delay(10);
  }

  // Give SD init a short retry window after USB reset/reboot.
  for (int attempt = 0; attempt < 5; attempt++) {
    if (Storage.begin()) {
      renderer.insertFont(PERF_FONT_ID, bookerly14FontFamily);
      runtimeInitialized = true;
      return true;
    }
    delay(200);
  }
  return false;
}

int findLargestSpineIndex(const std::shared_ptr<Epub>& epub, size_t* largestSize) {
  const int spineCount = epub->getSpineItemsCount();
  if (spineCount <= 0) {
    return -1;
  }

  int bestIndex = -1;
  size_t bestSize = 0;

  for (int i = 0; i < spineCount; i++) {
    const auto spineItem = epub->getSpineItem(i);
    size_t itemSize = 0;
    if (!epub->getItemSize(spineItem.href, &itemSize)) {
      continue;
    }
    if (itemSize > bestSize) {
      bestSize = itemSize;
      bestIndex = i;
    }
  }

  if (largestSize) {
    *largestSize = bestSize;
  }
  return bestIndex;
}

ProfileRunResult runProfileTrials(const std::shared_ptr<Epub>& epub, int spineIndex,
                                  const EpubProcessingProfile& profile, const char* profileName) {
  ProfileRunResult result;
  result.trialTimesMs.reserve(PERF_TRIALS);

  for (uint8_t trial = 0; trial < PERF_TRIALS; trial++) {
    Section section(epub, spineIndex, renderer);
    section.clearCache();

    const uint32_t start = millis();
    const bool success =
        section.createSectionFile(PERF_FONT_ID, PERF_LINE_COMPRESSION, PERF_EXTRA_PARAGRAPH_SPACING,
                                  PERF_PARAGRAPH_ALIGNMENT, PERF_VIEWPORT_WIDTH, PERF_VIEWPORT_HEIGHT,
                                  PERF_HYPHENATION_ENABLED, PERF_EMBEDDED_STYLE_ENABLED, nullptr, profile);
    const uint32_t elapsedMs = millis() - start;

    if (!success) {
      Serial.printf("[PERF] %s trial %u failed after %lu ms\n", profileName, trial + 1, elapsedMs);
      return result;
    }

    if (trial == 0) {
      result.pageCount = section.pageCount;
    }

    result.trialTimesMs.push_back(elapsedMs);
    Serial.printf("[PERF] %s trial %u: %lu ms (%u pages)\n", profileName, trial + 1, elapsedMs, section.pageCount);
    section.clearCache();
  }

  result.medianMs = median(result.trialTimesMs);
  result.success = true;
  return result;
}

}  // namespace

void test_large_epub_processing_optimized_vs_baseline() {
  if (!initRuntime()) {
    TEST_IGNORE_MESSAGE("Storage init failed (SD card not detected/ready)");
  }

  if (!Storage.exists(EPUB_PERF_TEST_BOOK_PATH)) {
    TEST_IGNORE_MESSAGE("EPUB_PERF_TEST_BOOK_PATH not found on SD card");
  }

  auto epub = std::make_shared<Epub>(EPUB_PERF_TEST_BOOK_PATH, PERF_CACHE_DIR);
  if (!epub->load(true, false)) {
    TEST_FAIL_MESSAGE("Failed to load EPUB metadata");
  }

  size_t largestSpineSize = 0;
  const int spineIndex = findLargestSpineIndex(epub, &largestSpineSize);
  if (spineIndex < 0 || largestSpineSize == 0) {
    TEST_FAIL_MESSAGE("Could not resolve a non-empty spine item for benchmarking");
  }

  Serial.printf("[PERF] benchmarking book=%s spine=%d size=%u bytes\n", EPUB_PERF_TEST_BOOK_PATH, spineIndex,
                static_cast<unsigned>(largestSpineSize));

  const auto baseline = runProfileTrials(epub, spineIndex, EpubProcessingProfile::baseline(), "baseline");
  TEST_ASSERT_TRUE_MESSAGE(baseline.success, "Baseline profile failed");

  const auto optimized = runProfileTrials(epub, spineIndex, EpubProcessingProfile::optimized(), "optimized");
  TEST_ASSERT_TRUE_MESSAGE(optimized.success, "Optimized profile failed");

  TEST_ASSERT_EQUAL_UINT16_MESSAGE(baseline.pageCount, optimized.pageCount,
                                   "Page count mismatch between baseline and optimized profiles");

  Serial.printf("[PERF] baseline median: %lu ms\n", baseline.medianMs);
  Serial.printf("[PERF] optimized median: %lu ms\n", optimized.medianMs);

  TEST_ASSERT_TRUE_MESSAGE(optimized.medianMs * 100 <= baseline.medianMs * (100 + PERF_ALLOWED_SLOWDOWN_PERCENT),
                           "Optimized profile regressed beyond allowed threshold");
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_large_epub_processing_optimized_vs_baseline);
  UNITY_END();
}

void loop() {}
