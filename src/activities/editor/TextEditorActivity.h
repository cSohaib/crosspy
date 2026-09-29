#pragma once

#include "TextEditorSupport.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/OptionPopup.h"

class TextEditorActivity final : public KeyboardEntryActivity {
 public:
  TextEditorActivity(GfxRenderer& renderer, MappedInputManager& input, std::string path, bool existing);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool handleHomeGesture() override;

 private:
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Orientation::Portrait;
  std::string path;
  bool existing;
  bool dirty = false;
  bool loaded = false;
  bool bom = false;
  bool crlf = false;
  bool pendingSaveError = false;
  size_t top = 0;
  mutable char lineBuffer[text_editor::LINE_BYTES + 1]{};
  OptionPopup popup;
  StrId error = StrId::STR_EDITOR_READ_ERROR;

  int fieldTop() const;
  int fieldLeft() const;
  int fieldWidth() const;
  int visibleRows() const;
  text_editor::Line lineAt(size_t start) const;
  void movePage(int direction);
  void showError(StrId message);
  bool saveFile();
  void insertUtf8(const char* value) override;
  bool backspaceUtf8() override;
  void onComplete(const std::string&) override;
  void onCancel() override;
  bool renderCustomField() override;
  bool cursorPositionFromPoint(int x, int y, size_t& position) const override;
  const char* enterLabel() const override;
  void textChanged() override { dirty = true; }
};
