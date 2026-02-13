#include "EpubReaderActivity.h"

#include <cstdint>

#include <Epub/Page.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "ReaderActivityHelpers.h"
#include "fontIds.h"
#include "util/DisplayTaskHelpers.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr int statusBarMargin = 19;
constexpr size_t legacyProgressSize = 4;
constexpr size_t progressSizeWithSectionPageCount = 6;
constexpr uint8_t maxSectionBuildRetries = 2;

int mapPageBySectionPortion(const int savedPage, const uint16_t savedPageCount, const uint16_t targetPageCount) {
  if (targetPageCount == 0 || targetPageCount == 1 || savedPageCount <= 1) {
    return 0;
  }

  int clampedSavedPage = savedPage;
  if (clampedSavedPage < 0) {
    clampedSavedPage = 0;
  } else if (clampedSavedPage >= savedPageCount) {
    clampedSavedPage = savedPageCount - 1;
  }

  const int64_t numerator = static_cast<int64_t>(clampedSavedPage) * (targetPageCount - 1);
  int mappedPage = static_cast<int>((numerator + ((savedPageCount - 1) / 2)) / (savedPageCount - 1));
  if (mappedPage < 0) {
    mappedPage = 0;
  } else if (mappedPage >= targetPageCount) {
    mappedPage = targetPageCount - 1;
  }
  return mappedPage;
}
}  // namespace

void EpubReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<EpubReaderActivity*>(param);
  DisplayTaskHelpers::displayLoop(
      self->updateRequired, self->renderingMutex, [self] { self->renderScreen(); },
      [self] { self->longPressHandler.onRenderComplete(); });
}

void EpubReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!epub) {
    return;
  }

  ReaderActivityHelpers::applyReaderOrientation(renderer);

  renderingMutex = xSemaphoreCreateMutex();

  epub->setupCacheDir();

  FsFile f;
  if (SdMan.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[progressSizeWithSectionPageCount];
    const int bytesRead = f.read(data, sizeof(data));
    if (bytesRead >= legacyProgressSize) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      progressRestoreSpineIndex = currentSpineIndex;
      hasPendingProgressRestore = true;
      hasProgressRestoreSectionPageCount = bytesRead >= progressSizeWithSectionPageCount;
      if (hasProgressRestoreSectionPageCount) {
        progressRestoreSectionPageCount = data[4] + (data[5] << 8);
        Serial.printf("[%lu] [ERS] Loaded cache: spine %d, page %d/%d\n", millis(), currentSpineIndex, nextPageNumber,
                      progressRestoreSectionPageCount);
      } else {
        progressRestoreSectionPageCount = 0;
        Serial.printf("[%lu] [ERS] Loaded legacy cache: spine %d, page %d\n", millis(), currentSpineIndex,
                      nextPageNumber);
      }
    }
    f.close();
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      Serial.printf("[%lu] [ERS] Opened for first time, navigating to text reference at index %d\n", millis(),
                    textSpineIndex);
    }
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath());

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&EpubReaderActivity::taskTrampoline, "EpubReaderActivityTask",
              8192,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void EpubReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  ReaderActivityHelpers::resetToUiOrientation(renderer);

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  DisplayTaskHelpers::stopTask(renderingMutex, displayTaskHandle);
  section.reset();
  epub.reset();
}

void EpubReaderActivity::loop() {
  // Pass input responsibility to sub activity if exists
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Enter chapter selection activity
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Don't start activity transition while rendering
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    const int currentPage = section ? section->currentPage : 0;
    const int totalPages = section ? section->pageCount : 0;
    exitActivity();
    enterNewActivity(new EpubReaderChapterSelectionActivity(
        this->renderer, this->mappedInput, epub, epub->getPath(), currentSpineIndex, currentPage, totalPages,
        [this] {
          exitActivity();
          updateRequired = true;
        },
        [this](const int newSpineIndex) {
          if (currentSpineIndex != newSpineIndex) {
            currentSpineIndex = newSpineIndex;
            nextPageNumber = 0;
            section.reset();
          }
          exitActivity();
          updateRequired = true;
        },
        [this](const int newSpineIndex, const int newPage) {
          // Handle sync position
          if (currentSpineIndex != newSpineIndex || (section && section->currentPage != newPage)) {
            currentSpineIndex = newSpineIndex;
            nextPageNumber = newPage;
            section.reset();
          }
          exitActivity();
          updateRequired = true;
        }));
    xSemaphoreGive(renderingMutex);
  }

  // Long press BACK goes directly to home
  if (mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= SETTINGS.getLongPressMs()) {
    onGoHome();
    return;
  }

  // Short press BACK goes to file selection
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < SETTINGS.getLongPressMs()) {
    onGoBack();
    return;
  }

  const auto pageTurnInput = ReaderActivityHelpers::readPageTurnInputState(mappedInput);

  // Immediate medium-press skip detection (trigger as soon as held threshold reached)
  if (SETTINGS.longPressChapterSkip) {
    longPressHandler.observePressRelease(pageTurnInput.anyWasPressed, pageTurnInput.anyWasReleased);

    auto result =
        longPressHandler.poll(pageTurnInput.prevPressed, pageTurnInput.nextPressed, mappedInput.getHeldTime(),
                              SETTINGS.getMediumPressMs(),
                              SETTINGS.getLongPressMs(), SETTINGS.longPressRepeat);
    if (result.mediumPrev) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      nextPageNumber = 0;
      currentSpineIndex = currentSpineIndex - 1;
      section.reset();
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      return;
    }
    if (result.mediumNext) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      nextPageNumber = 0;
      currentSpineIndex = currentSpineIndex + 1;
      section.reset();
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      return;
    }
  }

  if (!pageTurnInput.prevReleased && !pageTurnInput.nextReleased) {
    return;
  }

  // any button press when at end of the book goes back to the last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount() - 1;
    nextPageNumber = UINT16_MAX;
    updateRequired = true;
    return;
  }

  // If the release occurred after a medium/long hold, do not treat it as a short press
  if (longPressHandler.suppressRelease(mappedInput.getHeldTime(), SETTINGS.getMediumPressMs(),
                                       pageTurnInput.prevReleased, pageTurnInput.nextReleased)) {
    // consume the release; new-cycle rearming is handled by the state machine
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    updateRequired = true;
    return;
  }
  // (handled above) release after hold is consumed

  if (pageTurnInput.prevReleased) {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      nextPageNumber = UINT16_MAX;
      currentSpineIndex--;
      section.reset();
      xSemaphoreGive(renderingMutex);
    }
    updateRequired = true;
  } else {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      nextPageNumber = 0;
      currentSpineIndex++;
      section.reset();
      xSemaphoreGive(renderingMutex);
    }
    updateRequired = true;
  }
}

// TODO: Failure handling
void EpubReaderActivity::renderScreen() {
  if (!epub) {
    return;
  }

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, "End of book", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;
  if (SETTINGS.statusBar != CrossPointSettings::STATUS_BAR_MODE::NONE) {
    orientedMarginBottom += statusBarMargin;
  }

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    Serial.printf("[%lu] [ERS] Loading file: %s, index: %d\n", millis(), filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));
    bool sectionReindexed = false;

    const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

    if (!section->loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                  viewportHeight, SETTINGS.hyphenationEnabled)) {
      Serial.printf("[%lu] [ERS] Cache not found, building...\n", millis());

      auto progressBox = ReaderActivityHelpers::makeIndexingProgressBox(renderer);

      // Clear any prior transitional status UI (e.g. "Preparing metadata...").
      renderer.clearScreen();

      // Always show "Indexing..." text first
      {
        ReaderActivityHelpers::drawIndexingProgressTextOnly(renderer, progressBox);
        pagesUntilFullRefresh = 0;
      }

      // Setup callback - only called for chapters >= 50KB, redraws with progress bar
      auto progressSetup = [this, &progressBox] {
        ReaderActivityHelpers::drawIndexingProgressWithBar(renderer, progressBox);
        yield();
      };

      // Progress callback to update progress bar
      auto progressCallback = [this, &progressBox](int progress) {
        ReaderActivityHelpers::updateIndexingProgressWithBar(renderer, progressBox, progress);
        yield();
      };

      if (!section->createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                      SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                      viewportHeight, SETTINGS.hyphenationEnabled, progressSetup, progressCallback)) {
        Serial.printf("[%lu] [ERS] Failed to persist page data to SD\n", millis());
        section.reset();
        if (sectionBuildRetryCount < maxSectionBuildRetries) {
          sectionBuildRetryCount++;
          Serial.printf("[%lu] [ERS] Retrying section build (%u/%u)\n", millis(), sectionBuildRetryCount,
                        maxSectionBuildRetries);
          updateRequired = true;
          vTaskDelay(30 / portTICK_PERIOD_MS);
          return;
        }

        sectionBuildRetryCount = 0;
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, 300, "Failed to index section", true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(SMALL_FONT_ID, 325, "Press page key to retry");
        renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
        return;
      }
      sectionBuildRetryCount = 0;
      sectionReindexed = true;
    } else {
      sectionBuildRetryCount = 0;
      Serial.printf("[%lu] [ERS] Cache found, skipping build...\n", millis());
    }

    if (nextPageNumber == UINT16_MAX) {
      section->currentPage = section->pageCount - 1;
    } else {
      int targetPage = nextPageNumber;
      if (sectionReindexed && hasPendingProgressRestore && hasProgressRestoreSectionPageCount &&
          progressRestoreSpineIndex == currentSpineIndex) {
        targetPage = mapPageBySectionPortion(nextPageNumber, progressRestoreSectionPageCount, section->pageCount);
        Serial.printf("[%lu] [ERS] Reindexed section: remapped page %d/%d -> %d/%d\n", millis(), nextPageNumber,
                      progressRestoreSectionPageCount, targetPage, section->pageCount);
      }

      if (section->pageCount == 0) {
        targetPage = 0;
      } else if (targetPage < 0) {
        targetPage = 0;
      } else if (targetPage >= section->pageCount) {
        targetPage = section->pageCount - 1;
      }

      section->currentPage = targetPage;
    }
    hasPendingProgressRestore = false;
    hasProgressRestoreSectionPageCount = false;
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    Serial.printf("[%lu] [ERS] No pages to render\n", millis());
    renderer.drawCenteredText(UI_12_FONT_ID, 300, "Empty chapter", true, EpdFontFamily::BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    Serial.printf("[%lu] [ERS] Page out of bounds: %d (max %d)\n", millis(), section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, "Out of bounds", true, EpdFontFamily::BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  {
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      Serial.printf("[%lu] [ERS] Failed to load page from SD - clearing section cache\n", millis());
      section->clearCache();
      section.reset();
      return renderScreen();
    }
    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    Serial.printf("[%lu] [ERS] Rendered page in %dms\n", millis(), millis() - start);
  }

  FsFile f;
  if (SdMan.openFileForWrite("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[progressSizeWithSectionPageCount];
    data[0] = currentSpineIndex & 0xFF;
    data[1] = (currentSpineIndex >> 8) & 0xFF;
    data[2] = section->currentPage & 0xFF;
    data[3] = (section->currentPage >> 8) & 0xFF;
    data[4] = section->pageCount & 0xFF;
    data[5] = (section->pageCount >> 8) & 0xFF;
    f.write(data, sizeof(data));
    f.close();
  }
}

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  ReaderActivityHelpers::displayWithRefreshCadence(renderer, pagesUntilFullRefresh);
  ReaderActivityHelpers::renderAntiAliasedText(renderer, SETTINGS.textAntiAliasing,
                                               [this, &page, orientedMarginLeft, orientedMarginTop] {
                                                 page->render(this->renderer, SETTINGS.getReaderFontId(),
                                                              orientedMarginLeft, orientedMarginTop);
                                               });
}

void EpubReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                         const int orientedMarginLeft) const {
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;
  ReaderActivityHelpers::renderStatusBar(
      renderer, orientedMarginRight, orientedMarginBottom, orientedMarginLeft, showBatteryPercentage,
      1,  // EPUB battery has +1px offset
      [this] {
        const float sectionChapterProg = static_cast<float>(section->currentPage) / section->pageCount;
        const float bookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100;

        char progressStr[32];
        snprintf(progressStr, sizeof(progressStr), "%d/%d  %.1f%%", section->currentPage + 1, section->pageCount,
                 bookProgress);
        return std::string(progressStr);
      },
      [this, orientedMarginRight, orientedMarginLeft, showBatteryPercentage](
          const ReaderActivityHelpers::StatusBarVisibility& statusBar, const int textY, const int progressTextWidth) {
        // Centered chapter title text with fall-back to available-width centering for long titles.
        const int rendererableScreenWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
        const int batterySize = statusBar.showBattery ? (showBatteryPercentage ? 50 : 20) : 0;
        const int titleMarginLeft = batterySize + 30;
        const int titleMarginRight = progressTextWidth + 30;

        int titleMarginLeftAdjusted = std::max(titleMarginLeft, titleMarginRight);
        int availableTitleSpace = rendererableScreenWidth - 2 * titleMarginLeftAdjusted;
        const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);

        std::string title = "Unnamed";
        if (tocIndex != -1) {
          title = epub->getTocItem(tocIndex).title;
          const int titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
          if (titleWidth > availableTitleSpace) {
            availableTitleSpace = rendererableScreenWidth - titleMarginLeft - titleMarginRight;
            titleMarginLeftAdjusted = titleMarginLeft;
          }
        }

        ReaderActivityHelpers::truncateWithEllipsisToFit(renderer, SMALL_FONT_ID, title, availableTitleSpace);
        const int titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());

        renderer.drawText(SMALL_FONT_ID,
                          titleMarginLeftAdjusted + orientedMarginLeft + (availableTitleSpace - titleWidth) / 2, textY,
                          title.c_str());
      });
}
