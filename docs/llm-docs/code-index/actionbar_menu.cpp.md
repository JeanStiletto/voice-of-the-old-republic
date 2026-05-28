# actionbar_menu.cpp (279 lines)

Implementation of the action bar submenu. Maintains per-slot selected-index
shadow kept in lock-step with engine column state. Guards against opening
inside active dialog panels to avoid corrupting dialog selection state.

## Declarations (in source order)

- L23 — `namespace acc::actionbar_menu`
- L25 — `namespace` (anonymous)
- L34 — `struct State`
  note: holds active flag + curSlot; module-local submenu session state.
- L44 — `int ClampIndex(void* mi, int slot)`
  note: clamps g_selectedIndex[slot] to [0, VariantCount) and returns the valid index; handles variant-count shrinkage between sessions.
- L52 — `void SpeakCurrentVariant(void* mi, int slot)`
- L69 — `int CurrentSelection(int slot)`
- L74 — `bool Open(int slot)`
- L147 — `bool IsActive()`
- L151 — `bool HandleInputEvent(int code, int value)`
- L272 — `void ForceDisarm(const char* reason)`
