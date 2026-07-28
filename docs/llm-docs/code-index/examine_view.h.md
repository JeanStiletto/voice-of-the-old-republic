# examine_view.h (64 lines)

Public contract for the synthetic Ö examine list view: Open/IsActive/
HandleInputEvent/ForceDisarm/Tick/PollWin32Hotkey lifecycle, plus shared
lookup helpers (EffectName, EffectIconName, ResolveFeatName, ResolveSpellName,
IsHostileCreature) reused by combat_query and stealth_watch. Input routing
lives in interact_hotkey.cpp, not here.

## Declarations (in source order)

- L23 — `namespace acc::examine_view`
- L27 — `const char* EffectName(int type)` — localized EFFECT_TYPES name, nullptr if unmapped
- L34 — `const char* EffectIconName(int iconId)` — localized effecticon.2da row name
- L41 — `bool ResolveFeatName(unsigned short featIdx, char*, size_t)`
- L47 — `bool ResolveSpellName(int spellId, char*, size_t)`
- L52 — `bool IsHostileCreature(void* serverObject)`
- L54 — `bool Open()`
- L55 — `bool IsActive()`
- L58 — `bool HandleInputEvent(int code, int value)` — press-edge only
- L60 — `void ForceDisarm(const char* reason)`
- L61 — `void Tick()`
- L62 — `void PollWin32Hotkey()`
