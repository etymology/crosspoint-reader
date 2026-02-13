#include <Arduino.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <unity.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

#ifndef DISPLAY_PERF_TRIALS
#define DISPLAY_PERF_TRIALS 3
#endif

#ifndef DISPLAY_DRIVER_VARIANT
#define DISPLAY_DRIVER_VARIANT "optimized"
#endif

#ifndef DISPLAY_PERF_MAX_BW_FULL_MEDIAN_MS
#define DISPLAY_PERF_MAX_BW_FULL_MEDIAN_MS 0
#endif

#ifndef DISPLAY_PERF_MAX_BW_HALF_MEDIAN_MS
#define DISPLAY_PERF_MAX_BW_HALF_MEDIAN_MS 0
#endif

#ifndef DISPLAY_PERF_MAX_GRAY_FULL_TOTAL_MEDIAN_MS
#define DISPLAY_PERF_MAX_GRAY_FULL_TOTAL_MEDIAN_MS 0
#endif

#ifndef DISPLAY_PERF_MAX_GRAY_HALF_TOTAL_MEDIAN_MS
#define DISPLAY_PERF_MAX_GRAY_HALF_TOTAL_MEDIAN_MS 0
#endif

namespace {

constexpr size_t PERF_TRIALS = DISPLAY_PERF_TRIALS;
static_assert(PERF_TRIALS > 0, "DISPLAY_PERF_TRIALS must be greater than zero");
constexpr uint16_t DISPLAY_WIDTH_BYTES = HalDisplay::DISPLAY_WIDTH_BYTES;
constexpr uint16_t DISPLAY_HEIGHT = HalDisplay::DISPLAY_HEIGHT;
constexpr uint32_t DISPLAY_BUFFER_SIZE = HalDisplay::BUFFER_SIZE;

using TrialSamples = std::array<uint32_t, PERF_TRIALS>;

struct BwPerfResult {
  bool success = false;
  TrialSamples refreshTimesMs{};
  uint32_t medianMs = 0;
};

struct GrayPerfResult {
  bool success = false;
  TrialSamples baseRefreshTimesMs{};
  TrialSamples grayPassTimesMs{};
  TrialSamples totalTimesMs{};
  uint32_t baseMedianMs = 0;
  uint32_t grayPassMedianMs = 0;
  uint32_t totalMedianMs = 0;
};

HalDisplay display;
HalGPIO gpio;
bool runtimeInitialized = false;

template <size_t N>
uint32_t medianMs(std::array<uint32_t, N> values) {
  std::sort(values.begin(), values.end());
  const size_t mid = N / 2;
  if ((N % 2) == 0) {
    return static_cast<uint32_t>((values[mid - 1] + values[mid]) / 2);
  }
  return values[mid];
}

void logSamples(const char* label, const TrialSamples& samples) {
  Serial.printf("[DISP_PERF] %s samples:", label);
  for (size_t i = 0; i < samples.size(); i++) {
    Serial.printf(" %lu", samples[i]);
  }
  Serial.println(" ms");
}

bool initRuntime() {
  if (runtimeInitialized) {
    return true;
  }

  gpio.begin();

  Serial.begin(115200);
  const uint32_t serialStart = millis();
  while (!Serial && (millis() - serialStart) < 2000) {
    delay(10);
  }

  display.begin();
  display.clearScreen(0xFF);
  display.displayBuffer(HalDisplay::FULL_REFRESH);

  Serial.printf("[DISP_PERF] driver_variant=%s trials=%u\n", DISPLAY_DRIVER_VARIANT, static_cast<unsigned>(PERF_TRIALS));

  runtimeInitialized = true;
  return true;
}

void fillBwPattern(uint8_t* buffer, uint8_t phase) {
  constexpr uint8_t tileWidthBytes = 6;  // 48px
  constexpr uint8_t tileHeight = 24;

  for (uint16_t y = 0; y < DISPLAY_HEIGHT; y++) {
    const uint32_t rowOffset = static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    const uint8_t yTile = (y / tileHeight) & 0x1;

    for (uint16_t xByte = 0; xByte < DISPLAY_WIDTH_BYTES; xByte++) {
      const uint8_t xTile = (xByte / tileWidthBytes) & 0x1;
      const bool black = ((xTile ^ yTile ^ (phase & 0x1)) == 0);
      buffer[rowOffset + xByte] = black ? 0x00 : 0xFF;
    }
  }
}

void fillGraySceneBuffers(uint8_t* bwBuffer, uint8_t* lsbBuffer, uint8_t* msbBuffer, uint8_t phase) {
  memset(lsbBuffer, 0x00, DISPLAY_BUFFER_SIZE);
  memset(msbBuffer, 0x00, DISPLAY_BUFFER_SIZE);

  constexpr uint16_t bandCount = 4;
  constexpr uint16_t bandWidthBytes = DISPLAY_WIDTH_BYTES / bandCount;

  for (uint16_t y = 0; y < DISPLAY_HEIGHT; y++) {
    const uint32_t rowOffset = static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    for (uint16_t xByte = 0; xByte < DISPLAY_WIDTH_BYTES; xByte++) {
      uint16_t band = xByte / bandWidthBytes;
      if (band >= bandCount) {
        band = bandCount - 1;
      }
      if ((phase & 0x1) != 0) {
        band = (bandCount - 1) - band;
      }

      const uint32_t idx = rowOffset + xByte;
      switch (band) {
        case 0:  // black
          bwBuffer[idx] = 0x00;
          break;
        case 1:  // dark gray
          bwBuffer[idx] = 0x00;
          lsbBuffer[idx] = 0xFF;
          msbBuffer[idx] = 0xFF;
          break;
        case 2:  // light gray
          bwBuffer[idx] = 0x00;
          msbBuffer[idx] = 0xFF;
          break;
        default:  // white
          bwBuffer[idx] = 0xFF;
          break;
      }
    }
  }
}

BwPerfResult runBwScenario(const char* label, HalDisplay::RefreshMode mode) {
  BwPerfResult result;
  auto* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    return result;
  }

  Serial.printf("[DISP_PERF] scenario=%s\n", label);
  for (size_t trial = 0; trial < PERF_TRIALS; trial++) {
    fillBwPattern(frameBuffer, static_cast<uint8_t>(trial));
    const uint32_t startMs = millis();
    display.displayBuffer(mode);
    const uint32_t elapsedMs = millis() - startMs;

    result.refreshTimesMs[trial] = elapsedMs;
    Serial.printf("[DISP_PERF]   trial %u/%u -> %lu ms\n", static_cast<unsigned>(trial + 1), static_cast<unsigned>(PERF_TRIALS),
                  elapsedMs);
    delay(250);
  }

  result.medianMs = medianMs(result.refreshTimesMs);
  result.success = true;
  logSamples(label, result.refreshTimesMs);
  Serial.printf("[DISP_PERF] %s median: %lu ms\n", label, result.medianMs);
  return result;
}

GrayPerfResult runGrayScenario(const char* label, HalDisplay::RefreshMode baseMode) {
  GrayPerfResult result;

  auto* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    return result;
  }

  auto* bwBuffer = static_cast<uint8_t*>(malloc(DISPLAY_BUFFER_SIZE));
  auto* lsbBuffer = static_cast<uint8_t*>(malloc(DISPLAY_BUFFER_SIZE));
  auto* msbBuffer = static_cast<uint8_t*>(malloc(DISPLAY_BUFFER_SIZE));

  if (!bwBuffer || !lsbBuffer || !msbBuffer) {
    free(bwBuffer);
    free(lsbBuffer);
    free(msbBuffer);
    return result;
  }

  Serial.printf("[DISP_PERF] scenario=%s\n", label);

  for (size_t trial = 0; trial < PERF_TRIALS; trial++) {
    fillGraySceneBuffers(bwBuffer, lsbBuffer, msbBuffer, static_cast<uint8_t>(trial));
    memcpy(frameBuffer, bwBuffer, DISPLAY_BUFFER_SIZE);

    const uint32_t startMs = millis();
    display.displayBuffer(baseMode);
    const uint32_t baseDoneMs = millis();

    display.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
    display.displayGrayBuffer();
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
    display.cleanupGrayscaleBuffers(bwBuffer);
#endif
    const uint32_t endMs = millis();

    result.baseRefreshTimesMs[trial] = baseDoneMs - startMs;
    result.grayPassTimesMs[trial] = endMs - baseDoneMs;
    result.totalTimesMs[trial] = endMs - startMs;

    Serial.printf("[DISP_PERF]   trial %u/%u -> base=%lu ms gray=%lu ms total=%lu ms\n",
                  static_cast<unsigned>(trial + 1), static_cast<unsigned>(PERF_TRIALS),
                  result.baseRefreshTimesMs[trial], result.grayPassTimesMs[trial], result.totalTimesMs[trial]);
    delay(250);
  }

  free(msbBuffer);
  free(lsbBuffer);
  free(bwBuffer);

  result.baseMedianMs = medianMs(result.baseRefreshTimesMs);
  result.grayPassMedianMs = medianMs(result.grayPassTimesMs);
  result.totalMedianMs = medianMs(result.totalTimesMs);
  result.success = true;

  logSamples("gray base refresh", result.baseRefreshTimesMs);
  logSamples("gray lut pass", result.grayPassTimesMs);
  logSamples("gray total", result.totalTimesMs);
  Serial.printf("[DISP_PERF] %s median: base=%lu ms gray=%lu ms total=%lu ms\n", label, result.baseMedianMs,
                result.grayPassMedianMs, result.totalMedianMs);
  return result;
}

}  // namespace

void test_bw_refresh_speed_full_and_half() {
  if (!initRuntime()) {
    TEST_FAIL_MESSAGE("Failed to initialize display runtime");
  }

  const BwPerfResult fullBw = runBwScenario("bw_full_refresh", HalDisplay::FULL_REFRESH);
  TEST_ASSERT_TRUE_MESSAGE(fullBw.success, "BW full refresh benchmark failed");

  const BwPerfResult halfBw = runBwScenario("bw_half_refresh", HalDisplay::HALF_REFRESH);
  TEST_ASSERT_TRUE_MESSAGE(halfBw.success, "BW half refresh benchmark failed");

  Serial.printf("[DISP_PERF_SUMMARY] variant=%s bw_full_median_ms=%lu bw_half_median_ms=%lu\n", DISPLAY_DRIVER_VARIANT,
                fullBw.medianMs, halfBw.medianMs);

#if DISPLAY_PERF_MAX_BW_FULL_MEDIAN_MS > 0
  TEST_ASSERT_TRUE_MESSAGE(fullBw.medianMs <= DISPLAY_PERF_MAX_BW_FULL_MEDIAN_MS,
                           "BW full refresh median exceeded DISPLAY_PERF_MAX_BW_FULL_MEDIAN_MS");
#endif

#if DISPLAY_PERF_MAX_BW_HALF_MEDIAN_MS > 0
  TEST_ASSERT_TRUE_MESSAGE(halfBw.medianMs <= DISPLAY_PERF_MAX_BW_HALF_MEDIAN_MS,
                           "BW half refresh median exceeded DISPLAY_PERF_MAX_BW_HALF_MEDIAN_MS");
#endif
}

void test_gray_refresh_speed_with_full_and_half_base() {
  if (!initRuntime()) {
    TEST_FAIL_MESSAGE("Failed to initialize display runtime");
  }

  const GrayPerfResult grayFull = runGrayScenario("gray_with_full_base", HalDisplay::FULL_REFRESH);
  TEST_ASSERT_TRUE_MESSAGE(grayFull.success, "Grayscale + full base benchmark failed");

  const GrayPerfResult grayHalf = runGrayScenario("gray_with_half_base", HalDisplay::HALF_REFRESH);
  TEST_ASSERT_TRUE_MESSAGE(grayHalf.success, "Grayscale + half base benchmark failed");

  Serial.printf("[DISP_PERF_SUMMARY] variant=%s gray_full_total_median_ms=%lu gray_half_total_median_ms=%lu\n",
                DISPLAY_DRIVER_VARIANT, grayFull.totalMedianMs, grayHalf.totalMedianMs);

#if DISPLAY_PERF_MAX_GRAY_FULL_TOTAL_MEDIAN_MS > 0
  TEST_ASSERT_TRUE_MESSAGE(grayFull.totalMedianMs <= DISPLAY_PERF_MAX_GRAY_FULL_TOTAL_MEDIAN_MS,
                           "Gray/full total median exceeded DISPLAY_PERF_MAX_GRAY_FULL_TOTAL_MEDIAN_MS");
#endif

#if DISPLAY_PERF_MAX_GRAY_HALF_TOTAL_MEDIAN_MS > 0
  TEST_ASSERT_TRUE_MESSAGE(grayHalf.totalMedianMs <= DISPLAY_PERF_MAX_GRAY_HALF_TOTAL_MEDIAN_MS,
                           "Gray/half total median exceeded DISPLAY_PERF_MAX_GRAY_HALF_TOTAL_MEDIAN_MS");
#endif
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_bw_refresh_speed_full_and_half);
  RUN_TEST(test_gray_refresh_speed_with_full_and_half_base);
  UNITY_END();
}

void loop() {}
