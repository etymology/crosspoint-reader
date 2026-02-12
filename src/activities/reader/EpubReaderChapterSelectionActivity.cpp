#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "fontIds.h"
#include "util/DisplayTaskHelpers.h"
#include "util/ListNavigation.h"

namespace {
// Time threshold for treating a long press as a page-up/page-down now derived from settings
}  // namespace

bool EpubReaderChapterSelectionActivity::hasSyncOption() const { return KOREADER_STORE.hasCredentials(); }

int EpubReaderChapterSelectionActivity::getTotalItems() const {
  // Add 2 for sync options (top and bottom) if credentials are configured
  const int syncCount = hasSyncOption() ? 2 : 0;
  return epub->getTocItemsCount() + syncCount;
}

bool EpubReaderChapterSelectionActivity::isSyncItem(int index) const {
  if (!hasSyncOption()) return false;
  // First item and last item are sync options
  return index == 0 || index == getTotalItems() - 1;
}

int EpubReaderChapterSelectionActivity::tocIndexFromItemIndex(int itemIndex) const {
  // Account for the sync option at the top
  const int offset = hasSyncOption() ? 1 : 0;
  return itemIndex - offset;
}

int EpubReaderChapterSelectionActivity::getPageItems() const {
  // Layout constants used in renderScreen
  constexpr int startY = 60;
  constexpr int lineHeight = 30;

  const int screenHeight = renderer.getScreenHeight();
  const int endY = screenHeight - lineHeight;

  const int availableHeight = endY - startY;
  int items = availableHeight / lineHeight;

  // Ensure we always have at least one item per page to avoid division by zero
  if (items < 1) {
    items = 1;
  }
  return items;
}

void EpubReaderChapterSelectionActivity::taskTrampoline(void* param) {
  auto* self = static_cast<EpubReaderChapterSelectionActivity*>(param);
  DisplayTaskHelpers::displayLoop(
      self->updateRequired, self->renderingMutex, [self] { self->renderScreen(); },
      [self] { self->longPressHandler.onRenderComplete(); }, [self] { return !self->subActivity; });
}

void EpubReaderChapterSelectionActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!epub) {
    return;
  }

  renderingMutex = xSemaphoreCreateMutex();

  // Account for sync option offset when finding current TOC index
  const int syncOffset = hasSyncOption() ? 1 : 0;
  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }
  selectorIndex += syncOffset;  // Offset for top sync option

  // Trigger first update
  updateRequired = true;
  xTaskCreate(&EpubReaderChapterSelectionActivity::taskTrampoline, "EpubReaderChapterSelectionActivityTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void EpubReaderChapterSelectionActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  DisplayTaskHelpers::stopTask(renderingMutex, displayTaskHandle);
}

void EpubReaderChapterSelectionActivity::launchSyncActivity() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();
  enterNewActivity(new KOReaderSyncActivity(
      renderer, mappedInput, epub, epubPath, currentSpineIndex, currentPage, totalPagesInSpine,
      [this]() {
        // On cancel
        exitActivity();
        updateRequired = true;
      },
      [this](int newSpineIndex, int newPage) {
        // On sync complete
        exitActivity();
        onSyncPosition(newSpineIndex, newPage);
      }));
  xSemaphoreGive(renderingMutex);
}

void EpubReaderChapterSelectionActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right);

  const int pageItems = getPageItems();
  const int totalItems = getTotalItems();

  // Immediate skip while held (state machine)
  const bool prevPressed =
      mappedInput.isPressed(MappedInputManager::Button::Up) || mappedInput.isPressed(MappedInputManager::Button::Left);
  const bool nextPressed = mappedInput.isPressed(MappedInputManager::Button::Down) ||
                           mappedInput.isPressed(MappedInputManager::Button::Right);

  // Centralized long-press handling
  const bool anyWasPressed = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                             mappedInput.wasPressed(MappedInputManager::Button::Left) ||
                             mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                             mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool anyWasReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
  longPressHandler.observePressRelease(anyWasPressed, anyWasReleased);

  auto result = longPressHandler.poll(prevPressed, nextPressed, mappedInput.getHeldTime(), SETTINGS.getMediumPressMs(),
                                      SETTINGS.getLongPressMs(), SETTINGS.longPressRepeat);
  if (result.mediumPrev) {
    selectorIndex = ListNavigation::prevPage(selectorIndex, pageItems, totalItems);
    updateRequired = true;
    return;
  }
  if (result.mediumNext) {
    selectorIndex = ListNavigation::nextPage(selectorIndex, pageItems, totalItems);
    updateRequired = true;
    return;
  }

  const bool skipPage = mappedInput.getHeldTime() > SETTINGS.getMediumPressMs();
  if (skipPage && longPressHandler.suppressRelease(mappedInput.getHeldTime(), SETTINGS.getMediumPressMs(), prevReleased,
                                                   nextReleased)) {
    // Already handled during hold; consume this release until a new cycle
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Check if sync option is selected (first or last item)
    if (isSyncItem(selectorIndex)) {
      launchSyncActivity();
      return;
    }

    // Get TOC index (account for top sync offset)
    const int tocIndex = tocIndexFromItemIndex(selectorIndex);
    const auto newSpineIndex = epub->getSpineIndexForTocIndex(tocIndex);
    if (newSpineIndex == -1) {
      onGoBack();
    } else {
      onSelectSpineIndex(newSpineIndex);
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoBack();
  } else if (prevReleased) {
    selectorIndex = skipPage ? ListNavigation::prevPage(selectorIndex, pageItems, totalItems)
                             : ListNavigation::prevItem(selectorIndex, totalItems);
    updateRequired = true;
  } else if (nextReleased) {
    selectorIndex = skipPage ? ListNavigation::nextPage(selectorIndex, pageItems, totalItems)
                             : ListNavigation::nextItem(selectorIndex, totalItems);
    updateRequired = true;
  }
}

void EpubReaderChapterSelectionActivity::renderScreen() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const int pageItems = getPageItems();
  const int totalItems = getTotalItems();

  const std::string title =
      renderer.truncatedText(UI_12_FONT_ID, epub->getTitle().c_str(), pageWidth - 40, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, 15, title.c_str(), true, EpdFontFamily::BOLD);

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;
  renderer.fillRect(0, 60 + (selectorIndex % pageItems) * 30 - 2, pageWidth - 1, 30);

  for (int itemIndex = pageStartIndex; itemIndex < totalItems && itemIndex < pageStartIndex + pageItems; itemIndex++) {
    const int displayY = 60 + (itemIndex % pageItems) * 30;
    const bool isSelected = (itemIndex == selectorIndex);

    if (isSyncItem(itemIndex)) {
      // Draw sync option (at top or bottom)
      renderer.drawText(UI_10_FONT_ID, 20, displayY, ">> Sync Progress", !isSelected);
    } else {
      // Draw TOC item (account for top sync offset)
      const int tocIndex = tocIndexFromItemIndex(itemIndex);
      auto item = epub->getTocItem(tocIndex);
      const int indentSize = 20 + (item.level - 1) * 15;
      const std::string chapterName =
          renderer.truncatedText(UI_10_FONT_ID, item.title.c_str(), pageWidth - 40 - indentSize);
      renderer.drawText(UI_10_FONT_ID, indentSize, 60 + (tocIndex % pageItems) * 30, chapterName.c_str(),
                        tocIndex != selectorIndex);
    }
  }

  const auto labels = mappedInput.mapLabels("« Back", "Select", "Up", "Down");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
