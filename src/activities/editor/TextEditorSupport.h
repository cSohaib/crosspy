#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace text_editor {
constexpr size_t MAX_BYTES = 16 * 1024;
constexpr size_t LINE_BYTES = 192;

// Reject binary data and malformed UTF-8 rather than silently rewriting it.
inline bool validText(std::string_view s) {
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<uint8_t>(s[i++]);
    if (c < 0x80) {
      if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') return false;
      if (c == 0x7f) return false;
      continue;
    }
    int count;
    uint32_t cp;
    uint32_t minimum;
    if (c >= 0xc2 && c <= 0xdf) {
      count = 1;
      cp = c & 0x1f;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      count = 2;
      cp = c & 0x0f;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      count = 3;
      cp = c & 7;
      minimum = 0x10000;
    } else
      return false;
    while (count--) {
      if (i == s.size()) return false;
      const auto continuation = static_cast<uint8_t>(s[i++]);
      if ((continuation & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (continuation & 0x3f);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  }
  return true;
}

inline size_t nextChar(std::string_view s, size_t pos) {
  if (pos == s.size()) return pos;
  ++pos;
  while (pos < s.size() && (static_cast<uint8_t>(s[pos]) & 0xc0) == 0x80) ++pos;
  return pos;
}

struct Line {
  size_t start;
  size_t end;
  size_t next;
  bool hardBreak;
};

// One bounded scratch buffer is reused for both wrapping and rendering.
// Tabs display as one space; their original bytes remain in the document.
template <typename Measure>
Line lineAt(std::string_view text, size_t start, int width, char* scratch, Measure measure) {
  size_t end = start;
  scratch[0] = '\0';
  while (end < text.size() && text[end] != '\r' && text[end] != '\n') {
    const size_t next = nextChar(text, end);
    if (next - start > LINE_BYTES) break;
    memcpy(scratch + end - start, text.data() + end, next - end);
    if (text[end] == '\t') scratch[end - start] = ' ';
    scratch[next - start] = '\0';
    if (end > start && measure(scratch) > width) break;
    end = next;
  }
  scratch[end - start] = '\0';
  size_t next = end;
  const bool hard = end < text.size() && (text[end] == '\r' || text[end] == '\n');
  if (hard) {
    ++next;
    if (text[end] == '\r' && next < text.size() && text[next] == '\n') ++next;
  }
  return {start, end, next, hard};
}

inline bool containsCursor(const Line& line, size_t cursor, size_t size) {
  return cursor >= line.start && (cursor < line.end || ((line.hardBreak || line.end == size) && cursor <= line.end));
}

// Never truncate the original. Keep recovery files if installation or rollback fails.
// Adapter writeNew must exclusively create, write, close and verify the temporary file.
template <typename Files>
bool save(Files& files, const char* path, const char* temporary, const char* backup, std::string_view text,
          bool existing) {
  if (files.exists(temporary) || files.exists(backup) || files.exists(path) != existing) return false;
  if (!files.writeNew(temporary, text)) return false;
  if (existing && !files.rename(path, backup)) return false;
  if (!files.rename(temporary, path)) {
    if (existing) files.rename(backup, path);
    return false;
  }
  if (existing) files.remove(backup);
  return true;
}
}  // namespace text_editor
