# Crosspy text editor (Xteink X4 Pro)

The X4 Pro uses PlatformIO environment `x4pro` in `platformio.ini`. Its hardware
profile comes from FreeInk SDK; the editor lives in `src/activities/editor/` and
reuses `KeyboardEntryActivity` for keyboard layouts, touch routing and character
entry. Editor entry points are enabled only on the X4 Pro.

## Use

1. Open **Browse Files** and navigate to the desired folder.
2. Choose **New text file**, enter a filename, and confirm. `.txt` is appended if
   omitted. Existing filenames and invalid FAT filename characters are rejected.
3. To edit an existing `.txt` file, long-press it and choose **Edit text**.
   Normal Open continues to open the reader.
4. Tap the text to position the cursor. Use the keyboard to type, **Enter** to
   insert a newline, and **Del** to delete the previous character. Holding Del
   retains the existing keyboard's clear-all behavior.
5. The toolbar's Up/Down controls and physical side buttons move by a page of
   text, placing the cursor at the beginning of the target line. Text scrolls
   automatically to keep the cursor visible while typing.
6. Tap **Save** to save without leaving. **Save \*** indicates unsaved changes
   (or a file not yet created). Back or Home offers Save, Discard, or Cancel.

The filename appears in the header. The editor uses portrait layout (including
inverted portrait); if entered in landscape it temporarily switches to portrait
and restores the previous orientation when closed.

## File format and limits

- UTF-8 text, up to **16 KiB** including any UTF-8 BOM. Oversized or malformed
  files are refused; the editor never opens a truncated prefix for saving.
- Existing line endings and a UTF-8 BOM are preserved. Newlines inserted into a
  CRLF document use CRLF; otherwise they use LF. Backspace treats CRLF as a pair.
- Tabs remain unchanged in the file and display as one space. Text wraps at
  character boundaries; this is plain text editing, not Markdown rendering.
- Cursor hit testing is designed for left-to-right text. Complex bidirectional
  cursor behavior is outside this first version.
- Files are held in memory while editing. There is no autosave or persistent
  draft recovery before Save; a reset or power loss can lose unsaved typing.

## Saving and recovery

Saving exclusively creates `filename.txt.crosspy-tmp`, writes it, closes it, and
reopens it to verify every byte. For an existing file, the original is renamed
to `filename.txt.crosspy-bak` before the verified temporary file is installed.
The backup is removed only after installation succeeds. Failed installation
attempts to restore the backup.

FAT renames are not a power-loss transaction. Following an interrupted/failed
save, recovery files may remain beside the original. A subsequent save refuses
to overwrite those recovery files. Copy them to a computer first: `.crosspy-bak`
is the previous original; `.crosspy-tmp` is the attempted replacement (possibly
partial if the write itself failed). Restore the desired copy and remove the
recovery files before retrying. File reading caches are cleared after a
successful save.

## Implementation and validation

The keyboard string reserves 16 KiB once on editor entry. A 193-byte member
buffer is reused for wrapping, drawing and hit testing; no document-sized
render copy or per-line index is allocated. Entry checks available heap and
largest free block first. The heap allocation avoids placing the document on
the limited task stack. Render and input mutations share the render mutex.

Native regression tests cover UTF-8 validation, CR/LF/CRLF, empty and trailing
lines, soft wraps, multibyte cursor boundaries, the size limit, exclusive saves,
write failures, rename failures, rollback failure, and recovery-file collisions.
Run through the existing CMake suite, or directly:

```sh
c++ -std=c++20 -Wall -Wextra -Werror -pedantic -I. \
  test/text_editor/main.cpp -o /tmp/crosspy-editor-tests
/tmp/crosspy-editor-tests
```

Build and flash the X4 Pro:

```sh
pio run -e x4pro
pio run -e x4pro -t upload
```

Hardware acceptance checks:

- Create a file in an empty folder, type several pages, tap into an earlier
  line, insert accented characters and newlines, delete, save, close and reopen.
- Edit LF, CRLF and BOM files; inspect saved bytes on a computer.
- Test Save, Discard, Cancel, Home, and reopening the text in the reader.
- Confirm oversized files and a UTF-16/binary file cannot be edited.
- Verify portrait and inverted portrait touch alignment, and restoration after
  entry from either landscape orientation. Check all three themes.
- Verify Wi-Fi password and other ordinary keyboard screens still behave as
  before, including cursor taps, OK, Cancel and long-press key actions.
- Monitor the `TextEditor` heap log over repeated open/close cycles and check
  touch responsiveness and page navigation on the physical device.
