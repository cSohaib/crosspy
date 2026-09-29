#include "TextEditorActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <esp_heap_caps.h>

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace {
// Short SD writes and close failures leave the temporary file for recovery.
struct EditorFiles {
  bool bom;
  bool exists(const char* path) { return Storage.exists(path); }
  bool rename(const char* from, const char* to) { return Storage.rename(from, to); }
  bool remove(const char* path) { return Storage.remove(path); }
  bool writeNew(const char* path, std::string_view text) {
    {
      auto file = Storage.open(path, O_WRONLY | O_CREAT | O_EXCL);
      if (!file) return false;
      if (bom && file.write("\xef\xbb\xbf", 3) != 3) return false;
      if (file.write(text.data(), text.size()) != text.size()) return false;
      // Close before reopening for byte-for-byte verification.
      if (!file.close()) return false;
    }
    auto file = Storage.open(path);
    if (!file || file.fileSize64() != text.size() + (bom ? 3 : 0)) return false;
    if (bom && (file.read() != 0xef || file.read() != 0xbb || file.read() != 0xbf)) return false;
    char buffer[128];
    for (size_t offset = 0; offset < text.size();) {
      const size_t count = std::min(sizeof(buffer), text.size() - offset);
      if (file.read(buffer, count) != static_cast<int>(count) || memcmp(buffer, text.data() + offset, count))
        return false;
      offset += count;
    }
    return true;
  }
};
}  // namespace

TextEditorActivity::TextEditorActivity(GfxRenderer& renderer, MappedInputManager& input, std::string path,
                                       bool existing)
    : KeyboardEntryActivity(renderer, input, tr(STR_EDITOR_TITLE), "", text_editor::MAX_BYTES),
      path(std::move(path)),
      existing(existing) {
  title = this->path.substr(this->path.find_last_of('/') + 1);
}

void TextEditorActivity::onEnter() {
  previousOrientation = renderer.getOrientation();
  if (renderer.getScreenWidth() > renderer.getScreenHeight()) {
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  }
  // Reuse the keyboard's string, reserving once before typing. Keep 64 KiB free
  // for UI/font work; no full-document render copies or line-index arrays.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < text_editor::MAX_BYTES + 1 ||
      heap_caps_get_free_size(MALLOC_CAP_8BIT) < text_editor::MAX_BYTES + 65536) {
    error = StrId::STR_EDITOR_MEMORY_ERROR;
    KeyboardEntryActivity::onEnter();
    return;
  }
  text.reserve(text_editor::MAX_BYTES);
  loaded = !existing;
  if (existing) {
    auto file = Storage.open(path.c_str());
    if (file && !file.isDirectory()) {
      const uint64_t size = file.fileSize64();
      if (size > text_editor::MAX_BYTES) {
        error = StrId::STR_EDITOR_TOO_LARGE;
      } else {
        text.resize(size);
        if (file.read(text.data(), size) == static_cast<int>(size)) {
          loaded = text_editor::validText(text);
          if (!loaded) error = StrId::STR_EDITOR_ENCODING_ERROR;
        }
      }
    }
  }
  if (loaded) {
    bom = text.compare(0, 3, "\xef\xbb\xbf") == 0;
    if (bom) {
      text.erase(0, 3);
      maxLength -= 3;
    }
    const size_t firstNewline = text.find_first_of("\r\n");
    crlf = firstNewline != std::string::npos && text[firstNewline] == '\r' && firstNewline + 1 < text.size() &&
           text[firstNewline + 1] == '\n';
  }
  KeyboardEntryActivity::onEnter();
  cursorPos = 0;
  LOG_DBG("TextEditor", "Opened editor; free heap: %u",
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
}

void TextEditorActivity::onExit() {
  renderer.setOrientation(previousOrientation);
  KeyboardEntryActivity::onExit();
}

int TextEditorActivity::fieldLeft() const { return UITheme::getInstance().getMetrics().contentSidePadding; }
int TextEditorActivity::fieldWidth() const { return renderer.getScreenWidth() - 2 * fieldLeft(); }
int TextEditorActivity::fieldTop() const {
  const auto& m = UITheme::getInstance().getMetrics();
  return m.topPadding + m.headerHeight + GUI.getMenuRowHeight(renderer) + m.verticalSpacing;
}
int TextEditorActivity::visibleRows() const {
  return std::max(1, (keyboardRect().y - fieldTop() - UITheme::getInstance().getMetrics().verticalSpacing) /
                         renderer.getLineHeight(UI_12_FONT_ID));
}
text_editor::Line TextEditorActivity::lineAt(size_t start) const {
  return text_editor::lineAt(text, start, fieldWidth(), lineBuffer, [this](const char* value) {
    return renderer.getTextAdvanceX(UI_12_FONT_ID, value, EpdFontFamily::REGULAR);
  });
}

bool TextEditorActivity::renderCustomField() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int toolbarY = m.topPadding + m.headerHeight;
  const int cellWidth = renderer.getScreenWidth() / 4;
  const char* labels[] = {dirty || !existing ? tr(STR_EDITOR_SAVE_CHANGED) : tr(STR_SAVE), tr(STR_DIR_UP),
                          tr(STR_DIR_DOWN), tr(STR_BACK)};
  for (int i = 0; i < 4; ++i) {
    renderer.drawRect(i * cellWidth, toolbarY, cellWidth, GUI.getMenuRowHeight(renderer));
    renderer.drawText(UI_12_FONT_ID, i * cellWidth + m.verticalSpacing, toolbarY + m.verticalSpacing, labels[i]);
  }
  const int rows = visibleRows();
  const int height = renderer.getLineHeight(UI_12_FONT_ID);
  // Recompute the top line from the cursor after edits, including removed wraps.
  size_t start = 0;
  int cursorRow = 0;
  while (true) {
    const auto line = lineAt(start);
    if (text_editor::containsCursor(line, cursorPos, text.size()) || line.next == start) break;
    start = line.next;
    ++cursorRow;
  }
  int topRow = 0;
  start = 0;
  while (start < top && start < text.size()) {
    start = lineAt(start).next;
    ++topRow;
  }
  topRow = std::min(topRow, cursorRow);
  if (cursorRow >= topRow + rows) topRow = cursorRow - rows + 1;
  top = 0;
  for (int i = 0; i < topRow; ++i) top = lineAt(top).next;
  start = top;
  for (int row = 0; row < rows; ++row) {
    const auto line = lineAt(start);
    const int y = fieldTop() + row * height;
    renderer.drawText(UI_12_FONT_ID, fieldLeft(), y, lineBuffer);
    if (text_editor::containsCursor(line, cursorPos, text.size())) {
      lineBuffer[cursorPos - line.start] = '\0';
      const int x = fieldLeft() + renderer.getTextAdvanceX(UI_12_FONT_ID, lineBuffer, EpdFontFamily::REGULAR);
      renderer.fillRect(x, y, 2, height, true);
    }
    if (line.end == text.size()) break;
    start = line.next;
  }
  return true;
}

bool TextEditorActivity::cursorPositionFromPoint(int x, int y, size_t& position) const {
  const int height = renderer.getLineHeight(UI_12_FONT_ID);
  if (y < fieldTop() || y >= fieldTop() + visibleRows() * height || x < fieldLeft() || x > fieldLeft() + fieldWidth())
    return false;
  size_t start = top;
  const int target = (y - fieldTop()) / height;
  for (int row = 0; row < target; ++row) {
    const auto line = lineAt(start);
    if (line.end == text.size()) {
      position = text.size();
      return true;
    }
    start = line.next;
  }
  const auto line = lineAt(start);
  int previous = 0;
  for (size_t pos = start; pos < line.end;) {
    const size_t next = text_editor::nextChar(text, pos);
    const char saved = lineBuffer[next - start];
    lineBuffer[next - start] = '\0';
    const int width = renderer.getTextAdvanceX(UI_12_FONT_ID, lineBuffer, EpdFontFamily::REGULAR);
    lineBuffer[next - start] = saved;
    if (x - fieldLeft() < (previous + width) / 2) {
      position = pos;
      return true;
    }
    previous = width;
    pos = next;
  }
  position = line.end;
  return true;
}

void TextEditorActivity::movePage(int direction) {
  size_t start = 0;
  int row = 0;
  while (!text_editor::containsCursor(lineAt(start), cursorPos, text.size()) && start < text.size()) {
    start = lineAt(start).next;
    ++row;
  }
  const int target = std::max(0, row + direction * visibleRows());
  start = 0;
  for (int i = 0; i < target; ++i) {
    const auto line = lineAt(start);
    if (line.end == text.size()) break;
    start = line.next;
  }
  cursorPos = start;
  requestUpdate();
}

const char* TextEditorActivity::enterLabel() const { return tr(STR_EDITOR_ENTER); }
void TextEditorActivity::onComplete(const std::string&) {
  insertUtf8(crlf ? "\r\n" : "\n");
  requestUpdate();
}

void TextEditorActivity::showError(StrId message) {
  LOG_ERR("TextEditor", "Editor error %u: %s", static_cast<unsigned>(message), path.c_str());
  static constexpr StrId options[] = {StrId::STR_OK_BUTTON};
  popup.show(message, options, 1, 0, [](int) {});
  requestUpdate();
}

bool TextEditorActivity::saveFile() {
  if (!loaded) return false;
  if (existing && !dirty) return true;
  EditorFiles files{bom};
  const std::string temporary = path + ".crosspy-tmp";
  const std::string backup = path + ".crosspy-bak";
  if (!text_editor::save(files, path.c_str(), temporary.c_str(), backup.c_str(), text, existing)) {
    pendingSaveError = true;
    requestUpdate();
    return false;
  }
  existing = true;
  dirty = false;
  clearBookCache(path);
  requestUpdate();
  return true;
}

void TextEditorActivity::onCancel() {
  if (!loaded || (!dirty && existing)) {
    finish();
    return;
  }
  static constexpr StrId options[] = {StrId::STR_SAVE, StrId::STR_EDITOR_DISCARD, StrId::STR_CANCEL};
  popup.show(StrId::STR_EDITOR_UNSAVED, options, 3, 2, [this](int choice) {
    if (choice == 1 || (choice == 0 && saveFile())) finish();
  });
  requestUpdate();
}

bool TextEditorActivity::handleHomeGesture() {
  RenderLock lock(*this);
  if (!popup.isActive()) onCancel();
  return true;
}

void TextEditorActivity::loop() {
  {
    RenderLock lock(*this);
    if (!loaded) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
          mappedInput.wasReleased(MappedInputManager::Button::Confirm))
        finish();
      int x, y;
      if (mappedInput.wasScreenTapped(x, y)) finish();
      return;
    }
    if (pendingSaveError) {
      pendingSaveError = false;
      showError(StrId::STR_EDITOR_SAVE_ERROR);
      return;
    }
    if (popup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    int x, y;
    const int toolbarY =
        UITheme::getInstance().getMetrics().topPadding + UITheme::getInstance().getMetrics().headerHeight;
    if (mappedInput.wasScreenTapped(x, y) && y >= toolbarY && y < fieldTop()) {
      switch (std::min(3, std::max(0, x / (renderer.getScreenWidth() / 4)))) {
        case 0:
          saveFile();
          break;
        case 1:
          movePage(-1);
          break;
        case 2:
          movePage(1);
          break;
        case 3:
          onCancel();
          break;
      }
      return;
    }
    // Side buttons page the text on touch devices; the keyboard remains touch-operated.
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      movePage(-1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      movePage(1);
      return;
    }
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Up) || mappedInput.isPressed(MappedInputManager::Button::Down))
    return;
  KeyboardEntryActivity::loop();
}

void TextEditorActivity::render(RenderLock&& lock) {
  if (!loaded) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, I18N.get(error));
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2 + 2 * renderer.getLineHeight(UI_12_FONT_ID),
                              tr(STR_BACK));
    renderer.displayBuffer();
    return;
  }
  if (popup.processRender(renderer, mappedInput)) return;
  KeyboardEntryActivity::render(std::move(lock));
}

void TextEditorActivity::insertUtf8(const char* value) {
  if (!value || !*value) return;
  if (text.size() + strlen(value) > maxLength) {
    showError(StrId::STR_EDITOR_LIMIT);
    return;
  }
  KeyboardEntryActivity::insertUtf8(value);
}

bool TextEditorActivity::backspaceUtf8() {
  if (cursorPos >= 2 && text[cursorPos - 2] == '\r' && text[cursorPos - 1] == '\n') {
    text.erase(cursorPos - 2, 2);
    cursorPos -= 2;
    textChanged();
    return true;
  }
  return KeyboardEntryActivity::backspaceUtf8();
}
