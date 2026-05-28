# dialog_speech.cpp (276 lines)

Dialog speech implementation. Polls for Cinematic/Computer/BarkBubble panels, reads NPC line label + reply-listbox count, speaks on delta. Computer variant additionally iterates newly appended terminal rows. BarkBubble uses ReadFirstVisibleText fallback since label offset is undocumented.

## Declarations (in source order)

- L19 — `namespace acc::dialog_speech`
- L25 — `struct DialogPanelMatch` (anonymous namespace)
- L30 — `static DialogPanelMatch FindActiveDialogPanel()` (anonymous namespace)
- L59 — `static void* FindBarkBubblePanel()` (anonymous namespace)
- L80 — `static bool ReadLabelText(void* panel, size_t labelOffset, char* outBuf, size_t bufSize)` (anonymous namespace)
- L104 — `static int ReadListBoxRowCount(void* panel, size_t lbOffset)` (anonymous namespace)
- L122 — `static bool ReadFirstVisibleText(void* panel, char* outBuf, size_t bufSize)` (anonymous namespace)
- L147 — `void Tick()`
