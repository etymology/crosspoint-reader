#include "ReaderActivity.h"

#include "Epub.h"
#include "EpubReaderActivity.h"
#include "GfxRenderer.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "ReaderActivityHelpers.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
void renderPreparingFirstReadBox(GfxRenderer& renderer) {
  const auto prevOrientation = renderer.getOrientation();
  renderer.setOrientation(ReaderActivityHelpers::getReaderOrientation());

  constexpr int boxMargin = 20;
  constexpr int boxY = 50;
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, "Preparing metadata...");
  const int boxWidth = textWidth + boxMargin * 2;
  const int boxHeight = renderer.getLineHeight(UI_12_FONT_ID) + boxMargin * 2;
  const int boxX = (renderer.getScreenWidth() - boxWidth) / 2;

  renderer.fillRect(boxX, boxY, boxWidth, boxHeight, false);
  renderer.drawText(UI_12_FONT_ID, boxX + boxMargin, boxY + boxMargin, "Preparing metadata...");
  renderer.drawRect(boxX + 5, boxY + 5, boxWidth - 10, boxHeight - 10);
  renderer.displayBuffer();

  renderer.setOrientation(prevOrientation);
}
}  // namespace

std::string ReaderActivity::extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

bool ReaderActivity::isXtcFile(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".xtc") || StringUtils::checkFileExtension(path, ".xtch");
}

bool ReaderActivity::isTxtFile(const std::string& path) {
  if (path.length() < 4) return false;
  std::string ext4 = path.substr(path.length() - 4);
  return ext4 == ".txt" || ext4 == ".TXT";
}

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path, GfxRenderer& renderer) {
  if (!SdMan.exists(path.c_str())) {
    Serial.printf("[%lu] [   ] File does not exist: %s\n", millis(), path.c_str());
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.crosspoint"));
  // First try cache-only load. If it fails, we're about to do metadata/cache build.
  if (epub->load(false)) {
    return epub;
  }

  renderPreparingFirstReadBox(renderer);

  if (epub->load(true)) {
    return epub;
  }

  Serial.printf("[%lu] [   ] Failed to load epub\n", millis());
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    Serial.printf("[%lu] [   ] File does not exist: %s\n", millis(), path.c_str());
    return nullptr;
  }

  auto xtc = std::unique_ptr<Xtc>(new Xtc(path, "/.crosspoint"));
  if (xtc->load()) {
    return xtc;
  }

  Serial.printf("[%lu] [   ] Failed to load XTC\n", millis());
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    Serial.printf("[%lu] [   ] File does not exist: %s\n", millis(), path.c_str());
    return nullptr;
  }

  auto txt = std::unique_ptr<Txt>(new Txt(path, "/.crosspoint"));
  if (txt->load()) {
    return txt;
  }

  Serial.printf("[%lu] [   ] Failed to load TXT\n", millis());
  return nullptr;
}

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  const auto initialPath = fromBookPath.empty() ? "/" : extractFolderPath(fromBookPath);
  onGoToLibrary(initialPath, libraryTab);
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  exitActivity();
  enterNewActivity(new EpubReaderActivity(
      renderer, mappedInput, std::move(epub), [this, epubPath] { goToLibrary(epubPath); }, [this] { onGoBack(); }));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  exitActivity();
  enterNewActivity(new XtcReaderActivity(
      renderer, mappedInput, std::move(xtc), [this, xtcPath] { goToLibrary(xtcPath); }, [this] { onGoBack(); }));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  exitActivity();
  enterNewActivity(new TxtReaderActivity(
      renderer, mappedInput, std::move(txt), [this, txtPath] { goToLibrary(txtPath); }, [this] { onGoBack(); }));
}

void ReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (initialBookPath.empty()) {
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  currentBookPath = initialBookPath;

  if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      onGoBack();
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else {
    auto epub = loadEpub(initialBookPath, renderer);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}
