# tutorial_popup.h (42 lines)

Public surface for the on-demand synthetic tutorial popup used for the
voice-acted Endar Spire tutorial lines (Trask / end_pop*). Those lines
stay suppressed during their VO; at the next dialogue reply-prompt break
this mounts a REAL engine tutorial popup carrying the accumulated
keyboard hint(s), so it pauses/dismisses exactly like a stock tutorial.
See docs/llm-docs/tutorial-popup-mechanics.md.

## Declarations (in source order)

- L25 — `void RecordPendingHint(uint32_t strref, const char* hint)`
  note: accumulates hints (newline-joined) since the last break — Trask often delivers several lines before one
- L30 — `bool FirePendingAtReplyBreak()`
  note: returns true if it fired, so the caller can skip its own announce
- L35-36 — `bool SyntheticActive()` / `const char* SyntheticHint()`
  note: while true, TutorialBox speech paths (fingerprint override, listbox suppression, chain gate) route to the hint instead of the mouse-worded message
- L40 — `void PollDismiss()` — per-tick; detects the popup closing and clears state + unpauses
