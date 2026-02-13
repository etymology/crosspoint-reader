#pragma once

#include <string>

#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "fontIds.h"

namespace ReaderActivityHelpers {

struct PageTurnInputState {
  bool prevReleased = false;
  bool nextReleased = false;
  bool prevPressed = false;
  bool nextPressed = false;
  bool anyWasPressed = false;
  bool anyWasReleased = false;
};

struct StatusBarVisibility {
  bool showProgress = false;
  bool showBattery = false;
  bool showTitle = false;
};

struct IndexingProgressBox {
  int textWidth = 0;
  int lineHeight = 0;
  int boxWidthWithBar = 0;
  int boxWidthNoBar = 0;
  int boxHeightWithBar = 0;
  int boxHeightNoBar = 0;
  int boxXWithBar = 0;
  int boxXNoBar = 0;
  int barX = 0;
  int barY = 0;
  int lastFillWidth = 0;
};

inline GfxRenderer::Orientation getReaderOrientation() {
  switch (SETTINGS.orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      return GfxRenderer::Orientation::Portrait;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      return GfxRenderer::Orientation::LandscapeClockwise;
    case CrossPointSettings::ORIENTATION::INVERTED:
      return GfxRenderer::Orientation::PortraitInverted;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      return GfxRenderer::Orientation::LandscapeCounterClockwise;
    default:
      return GfxRenderer::Orientation::Portrait;
  }
}

inline void applyReaderOrientation(GfxRenderer& renderer) {
  renderer.setOrientation(getReaderOrientation());
}

inline void resetToUiOrientation(GfxRenderer& renderer) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}

inline PageTurnInputState readPageTurnInputState(const MappedInputManager& mappedInput) {
  const bool powerPageTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN;

  PageTurnInputState state;
  state.prevReleased = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                       mappedInput.wasReleased(MappedInputManager::Button::Left);
  state.nextReleased = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                       mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                       (powerPageTurn && mappedInput.wasReleased(MappedInputManager::Button::Power));
  state.prevPressed = mappedInput.isPressed(MappedInputManager::Button::PageBack) ||
                      mappedInput.isPressed(MappedInputManager::Button::Left);
  state.nextPressed = mappedInput.isPressed(MappedInputManager::Button::PageForward) ||
                      mappedInput.isPressed(MappedInputManager::Button::Right) ||
                      (powerPageTurn && mappedInput.isPressed(MappedInputManager::Button::Power));
  state.anyWasPressed = mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                        mappedInput.wasPressed(MappedInputManager::Button::Left) ||
                        mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
                        mappedInput.wasPressed(MappedInputManager::Button::Right) ||
                        mappedInput.wasPressed(MappedInputManager::Button::Power);
  state.anyWasReleased = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                         mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                         mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                         mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                         mappedInput.wasReleased(MappedInputManager::Button::Power);
  return state;
}

inline StatusBarVisibility getStatusBarVisibility() {
  const bool showProgress = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  const bool showBattery = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                           SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  StatusBarVisibility visibility;
  visibility.showProgress = showProgress;
  visibility.showBattery = showBattery;
  visibility.showTitle = showBattery;
  return visibility;
}

inline IndexingProgressBox makeIndexingProgressBox(const GfxRenderer& renderer) {
  constexpr int barWidth = 200;
  constexpr int barHeight = 10;
  constexpr int boxMargin = 20;
  constexpr int boxY = 50;

  IndexingProgressBox box;
  box.textWidth = renderer.getTextWidth(UI_12_FONT_ID, "Indexing...");
  box.lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  box.boxWidthWithBar = (barWidth > box.textWidth ? barWidth : box.textWidth) + boxMargin * 2;
  box.boxWidthNoBar = box.textWidth + boxMargin * 2;
  box.boxHeightWithBar = box.lineHeight + barHeight + boxMargin * 3;
  box.boxHeightNoBar = box.lineHeight + boxMargin * 2;
  box.boxXWithBar = (renderer.getScreenWidth() - box.boxWidthWithBar) / 2;
  box.boxXNoBar = (renderer.getScreenWidth() - box.boxWidthNoBar) / 2;
  box.barX = box.boxXWithBar + (box.boxWidthWithBar - barWidth) / 2;
  box.barY = boxY + box.lineHeight + boxMargin * 2;
  return box;
}

inline void drawIndexingProgressTextOnly(GfxRenderer& renderer, const IndexingProgressBox& box) {
  constexpr int boxMargin = 20;
  constexpr int boxY = 50;

  renderer.fillRect(box.boxXNoBar, boxY, box.boxWidthNoBar, box.boxHeightNoBar, false);
  renderer.drawText(UI_12_FONT_ID, box.boxXNoBar + boxMargin, boxY + boxMargin, "Indexing...");
  renderer.drawRect(box.boxXNoBar + 5, boxY + 5, box.boxWidthNoBar - 10, box.boxHeightNoBar - 10);
  renderer.displayBufferAsync();
}

inline void drawIndexingProgressWithBar(GfxRenderer& renderer, const IndexingProgressBox& box) {
  constexpr int barWidth = 200;
  constexpr int barHeight = 10;
  constexpr int boxMargin = 20;
  constexpr int boxY = 50;

  renderer.fillRect(box.boxXWithBar, boxY, box.boxWidthWithBar, box.boxHeightWithBar, false);
  renderer.drawText(UI_12_FONT_ID, box.boxXWithBar + boxMargin, boxY + boxMargin, "Indexing...");
  renderer.drawRect(box.boxXWithBar + 5, boxY + 5, box.boxWidthWithBar - 10, box.boxHeightWithBar - 10);
  renderer.drawRect(box.barX, box.barY, barWidth, barHeight);
  renderer.displayBufferAsync();
}

inline bool updateIndexingProgressWithBar(GfxRenderer& renderer, IndexingProgressBox& box, const int progressPercent) {
  constexpr int barWidth = 200;
  constexpr int barHeight = 10;

  const int boundedPercent = progressPercent < 0 ? 0 : (progressPercent > 100 ? 100 : progressPercent);
  const int fillWidth = (barWidth - 2) * boundedPercent / 100;
  if (fillWidth <= box.lastFillWidth) {
    return false;
  }

  const int deltaWidth = fillWidth - box.lastFillWidth;
  renderer.fillRect(box.barX + 1 + box.lastFillWidth, box.barY + 1, deltaWidth, barHeight - 2, true);
  if (renderer.displayBufferAsync(EInkDisplay::FAST_REFRESH)) {
    box.lastFillWidth = fillWidth;
    return true;
  }
  return false;
}

inline void truncateWithEllipsisToFit(const GfxRenderer& renderer, const int fontId, std::string& text,
                                      const int maxWidth) {
  int textWidth = renderer.getTextWidth(fontId, text.c_str());
  while (textWidth > maxWidth && text.length() > 11) {
    text.replace(text.length() - 8, 8, "...");
    textWidth = renderer.getTextWidth(fontId, text.c_str());
  }
}

template <typename BuildProgressTextFn, typename RenderTitleFn>
inline void renderStatusBar(GfxRenderer& renderer, const int orientedMarginRight, const int orientedMarginBottom,
                            const int orientedMarginLeft, const bool showBatteryPercentage, const int batteryXOffset,
                            BuildProgressTextFn buildProgressText, RenderTitleFn renderTitle) {
  const auto statusBar = getStatusBarVisibility();
  const int textY = renderer.getScreenHeight() - orientedMarginBottom - 4;
  int progressTextWidth = 0;

  if (statusBar.showProgress) {
    const auto progressText = buildProgressText();
    progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressText.c_str());
    renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - orientedMarginRight - progressTextWidth, textY,
                      progressText.c_str());
  }

  if (statusBar.showBattery) {
    ScreenComponents::drawBattery(renderer, orientedMarginLeft + batteryXOffset, textY, showBatteryPercentage);
  }

  if (statusBar.showTitle) {
    renderTitle(statusBar, textY, progressTextWidth);
  }
}

inline void displayWithRefreshCadence(GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
}

template <typename RenderFn>
inline void renderAntiAliasedText(GfxRenderer& renderer, const bool enabled, RenderFn renderFn) {
  if (!enabled) {
    return;
  }

  renderer.storeBwBuffer();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.restoreBwBuffer();
}

}  // namespace ReaderActivityHelpers
