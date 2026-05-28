# menus_extract.cpp (1639 lines)

Control-text extraction TU. The `FromControl` announce ladder (L344–L1637) walks a fixed sequence of extraction strategies (label/button text, strref, gui_string, slider value, listbox rows, portrait, per-kind hardcoded names) and returns the first non-empty result. Anonymous-namespace helpers are private to this TU.

## Declarations (in source order)

- L48 — `typedef uint32_t (__thiscall* PFN_CSWCCreatureGetPortraitId)(void*)` — engine accessor for portrait ID
- L53 — `typedef void* (__thiscall* PFN_CSWCCreatureGetPortrait)(void*, char*, int)` — engine accessor for live portrait resref
- L67 — `constexpr const char* kPortraitByRow[32]` — static fallback table of base resrefs indexed by portrait ID
- L89 — `struct CycleCategoryEntry` — maps a control pointer to its captured category label string (anonymous ns)
- L97 — `const char* LookupCycleCategory(void* control)` — returns cached category or nullptr (anonymous ns)
- L126 — `bool IsCycleFlankerArrow(void* panel, void* control)` — true when the control is a +/- flanker arrow in a chargen cycle widget; suppresses sibling-label fallback on these (anonymous ns)
- L197 — `const char* FindSiblingLabel(void* panel, void* control, char* outBuf, size_t bufSize)` — locates the nearest label sibling at the same x-coordinate; used as a last-resort name for image-only buttons (anonymous ns)
- L287 — `bool IsSoundOptionsMovieSlider(void* panel, void* control)` — true when the control is the movie-volume slider in Sound Options; suppresses its announce (anonymous ns)
- L319 — `void acc::menus::extract::ResetCycleCategoryCache()`
- L323 — `void acc::menus::extract::CaptureCycleCategory(void* control, const char* category)`
- L344 — `const char* acc::menus::extract::FromControl(void* control, char* outBuf, size_t bufSize, void* ownerPanel)` — the announce ladder; ~1290 lines covering: label text (section 0), button text (1), strref (2), gui_string (3), toggle state suffix append (inline), disabled-bit suffix append (inline), slider value+category (4-6), chargen stat rows (7), equip-stat rows (8), per-kind in-game-menu names (9a), equip slot names (9b), map prev/next note (9b2), workbench slot names (9b3), class-selection cache (9c), portrait resref decode (9d), sibling-label fallback (9 final), cycle category prefix (post-sections), toggle suffix (post), disabled suffix (post)
