#include "DeveloperTests.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <freertos/task.h>

#include "MappedInputManager.h"
#include "fontIds.h"

namespace {
constexpr TickType_t kPollDelay = pdMS_TO_TICKS(10);

bool isAnyButtonHeld(const MappedInputManager& mappedInput) {
  using Button = MappedInputManager::Button;
  return mappedInput.isPressed(Button::Back) || mappedInput.isPressed(Button::Confirm) ||
         mappedInput.isPressed(Button::Left) || mappedInput.isPressed(Button::Right) || mappedInput.isPressed(Button::Up) ||
         mappedInput.isPressed(Button::Down) || mappedInput.isPressed(Button::Power);
}

void waitForAnyButtonPress(const MappedInputManager& mappedInput) {
  // Avoid consuming a press that triggered entry into this test.
  do {
    mappedInput.update();
    vTaskDelay(kPollDelay);
  } while (isAnyButtonHeld(mappedInput));

  while (true) {
    mappedInput.update();
    if (mappedInput.wasAnyPressed()) {
      return;
    }
    vTaskDelay(kPollDelay);
  }
}

bool checkerColor(const int x, const int y, const int tileSize) { return ((x / tileSize) + (y / tileSize)) % 2 == 0; }

void fillCheckerboard(GfxRenderer& renderer, const int width, const int height, const int tileSize) {
  for (int y = 0; y < height; y += tileSize) {
    const int blockHeight = std::min(tileSize, height - y);
    for (int x = 0; x < width; x += tileSize) {
      const int blockWidth = std::min(tileSize, width - x);
      renderer.fillRect(x, y, blockWidth, blockHeight, checkerColor(x, y, tileSize));
    }
  }
}

void fillCheckerboardWindow(GfxRenderer& renderer, const int windowX, const int windowY, const int windowWidth,
                            const int windowHeight, const int tileSize, const bool inverted) {
  for (int y = windowY; y < windowY + windowHeight; y += tileSize) {
    const int blockHeight = std::min(tileSize, windowY + windowHeight - y);
    for (int x = windowX; x < windowX + windowWidth; x += tileSize) {
      const int blockWidth = std::min(tileSize, windowX + windowWidth - x);
      const bool black = checkerColor(x, y, tileSize) ^ inverted;
      renderer.fillRect(x, y, blockWidth, blockHeight, black);
    }
  }
}
}  // namespace

namespace DeveloperTests {

void runDisplayResponseTest(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const SemaphoreHandle_t renderingMutex) {
  LOG_DBG("TESTS", "Running checkerboard display window test");

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int tileSize = 32;
  const int middleSquareSize = std::min(pageWidth, pageHeight) / 2;
  const int windowX = (pageWidth - middleSquareSize) / 2;
  const int windowY = (pageHeight - middleSquareSize) / 2;
  const int windowWidth = middleSquareSize;
  const int windowHeight = middleSquareSize;

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  fillCheckerboard(renderer, pageWidth, pageHeight, tileSize);
  renderer.drawCenteredText(UI_10_FONT_ID, 14, "Checkerboard pattern - press any button", true, EpdFontFamily::BOLD);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  // Dual-buffer displayBuffer() swaps backing buffers; redraw baseline into the new active draw buffer
  // so subsequent window writes are applied over checkerboard, not over the previous white buffer.
  fillCheckerboard(renderer, pageWidth, pageHeight, tileSize);
  renderer.drawCenteredText(UI_10_FONT_ID, 14, "Checkerboard pattern - press any button", true, EpdFontFamily::BOLD);
  xSemaphoreGive(renderingMutex);
  waitForAnyButtonPress(mappedInput);

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  fillCheckerboardWindow(renderer, windowX, windowY, windowWidth, windowHeight, tileSize, true);
  renderer.drawRect(windowX, windowY, windowWidth, windowHeight, false);
  renderer.drawCenteredText(UI_10_FONT_ID, 14, "Middle square inverted - press any button", true, EpdFontFamily::BOLD);
  renderer.displayWindow(windowX, windowY, windowWidth, windowHeight);
  xSemaphoreGive(renderingMutex);
  waitForAnyButtonPress(mappedInput);

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  fillCheckerboardWindow(renderer, windowX, windowY, windowWidth, windowHeight, tileSize, false);
  renderer.drawRect(windowX, windowY, windowWidth, windowHeight, true);
  renderer.drawCenteredText(UI_10_FONT_ID, 14, "Middle square restored - press any button", true, EpdFontFamily::BOLD);
  renderer.displayWindow(windowX, windowY, windowWidth, windowHeight);
  xSemaphoreGive(renderingMutex);
  waitForAnyButtonPress(mappedInput);

  LOG_DBG("TESTS", "Checkerboard display window test complete");
}

}  // namespace DeveloperTests
