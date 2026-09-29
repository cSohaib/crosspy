#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "src/activities/editor/TextEditorSupport.h"

#define CHECK(expr)                                   \
  do {                                                \
    if (!(expr)) {                                    \
      std::cerr << __LINE__ << ": " << #expr << '\n'; \
      std::exit(1);                                   \
    }                                                 \
  } while (0)

struct Files {
  std::map<std::string, std::string> data;
  bool failWrite = false;
  bool failBackup = false;
  bool failInstall = false;
  bool failRollback = false;
  bool exists(const char* name) { return data.count(name); }
  bool writeNew(const char* name, std::string_view text) {
    CHECK(!exists(name));
    data[name] = failWrite ? "partial" : std::string(text);
    return !failWrite;
  }
  bool rename(const char* from, const char* to) {
    if ((std::string(from) == "file" && failBackup) || (std::string(from) == "tmp" && failInstall) ||
        (std::string(from) == "bak" && failRollback))
      return false;
    if (!exists(from) || exists(to)) return false;
    data[to] = data[from];
    data.erase(from);
    return true;
  }
  bool remove(const char* name) { return data.erase(name); }
};

int main() {
  using namespace text_editor;
  CHECK(validText(""));
  CHECK(validText("Hello\r\nWëllem\n\t世界 😀"));
  CHECK(validText("\xef\xbb\xbftext"));
  for (const auto& text :
       {std::string("a\0b", 3), std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
        std::string("\xe2\x82"), std::string("\x80"), std::string("\x1b[0m"), std::string("\x7f")})
    CHECK(!validText(text));

  char scratch[LINE_BYTES + 1];
  auto width = [](const char* text) {
    int count = 0;
    for (size_t i = 0; text[i]; ++i)
      if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) ++count;
    return count;
  };
  auto line = lineAt("ab\r\n\ncd\r", 0, 10, scratch, width);
  CHECK(line.end == 2 && line.next == 4 && line.hardBreak);
  CHECK(containsCursor(line, 2, 8));
  line = lineAt("ab\r\n\ncd\r", 4, 10, scratch, width);
  CHECK(line.start == 4 && line.end == 4 && line.next == 5 && line.hardBreak);
  line = lineAt("ab\r\n\ncd\r", 8, 10, scratch, width);
  CHECK(line.end == 8 && line.next == 8 && containsCursor(line, 8, 8));
  line = lineAt("é😀z", 0, 2, scratch, width);
  CHECK(line.end == 6 && line.next == 6 && !line.hardBreak);
  CHECK(std::string(scratch) == "é😀");
  CHECK(!containsCursor(line, 6, 7));
  line = lineAt("é😀z", 6, 2, scratch, width);
  CHECK(containsCursor(line, 6, 7));
  line = lineAt("😀x", 0, 0, scratch, width);
  CHECK(line.end == 4);  // Oversized glyph still makes progress.
  line = lineAt("a\tb", 0, 3, scratch, width);
  CHECK(std::string(scratch) == "a b");

  const std::string longText(MAX_BYTES, 'x');
  size_t start = 0;
  do {
    line = lineAt(longText, start, 1000, scratch, width);
    CHECK(line.end - line.start <= LINE_BYTES);
    CHECK(line.next > start);
    start = line.next;
  } while (start < longText.size());
  CHECK(start == MAX_BYTES);

  // Every byte of a valid document cursor has exactly one visual line owner,
  // except the interior of UTF-8 characters and CRLF pairs.
  for (const std::string text : {"", "\n", "a\n", "\n\n", "abcd", "é😀z", "a\r\nb"}) {
    for (int columns = 1; columns <= 6; ++columns) {
      std::vector<Line> lines;
      lines.reserve(text.size() + 1);
      start = 0;
      do {
        line = lineAt(text, start, columns, scratch, width);
        lines.push_back(line);
        if (line.end == text.size()) break;
        CHECK(line.next > start);
        start = line.next;
      } while (true);
      for (size_t cursor = 0; cursor <= text.size(); cursor = nextChar(text, cursor)) {
        if (!(cursor && cursor < text.size() && text[cursor - 1] == '\r' && text[cursor] == '\n')) {
          int owners = 0;
          for (const auto& candidate : lines) owners += containsCursor(candidate, cursor, text.size());
          CHECK(owners == 1);
        }
        if (cursor == text.size()) break;
      }
    }
  }

  Files files;
  CHECK(save(files, "file", "tmp", "bak", "", false));
  CHECK(files.data.at("file").empty());
  CHECK(save(files, "file", "tmp", "bak", "new", true));
  CHECK(files.data.at("file") == "new" && files.data.size() == 1);
  CHECK(!save(files, "file", "tmp", "bak", "overwrite", false));
  CHECK(files.data.at("file") == "new");
  for (int failure = 0; failure < 4; ++failure) {
    Files broken;
    broken.data["file"] = "original";
    broken.failWrite = failure == 0;
    broken.failBackup = failure == 1;
    broken.failInstall = failure >= 2;
    broken.failRollback = failure == 3;
    CHECK(!save(broken, "file", "tmp", "bak", "edited", true));
    CHECK(broken.data.at(failure == 3 ? "bak" : "file") == "original");
    CHECK(broken.data.at("tmp") == (failure == 0 ? "partial" : "edited"));
  }
  for (const char* recovery : {"tmp", "bak"}) {
    Files stale;
    stale.data["file"] = "original";
    stale.data[recovery] = "recovery";
    CHECK(!save(stale, "file", "tmp", "bak", "edited", true));
    CHECK(stale.data.at("file") == "original");
    CHECK(stale.data.at(recovery) == "recovery");
  }
  std::cout << "Text editor validation, layout and save-failure tests passed\n";
}
