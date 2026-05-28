# examine_view.h (37 lines)

Shift+H examine list view. Synthetic arrow-navigable list of pre-composed rows
for the focused/LastTarget creature. KOTOR's CSWGuiExamine is a plain TLK message
box, so we build our own from direct field reads. Self-disarms when target is lost.

## Declarations (in source order)

- L21 — `namespace acc::examine_view`
- L25 — `const char* EffectName(int type)`
  note: EFFECT_TYPES enum to localized display name; shared with combat_query's Q/E brief; returns nullptr for unmapped types
- L28 — `bool Open()`
- L29 — `bool IsActive()`
- L32 — `bool HandleInputEvent(int code, int value)`
  note: press-edge only (value != 0); called after queue / actionbar gates
- L34 — `void ForceDisarm(const char* reason)`
- L35 — `void Tick()`
- L36 — `void PollWin32Hotkey()`
