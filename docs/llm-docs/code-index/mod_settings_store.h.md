# mod_settings_store.h (39 lines)

Public typed get/set surface over the persistent settings file. Exists
because mod-settings toggles (Mod-Einstellungen) and the cue-volume slider
were otherwise in-memory-only and reset every launch; owners (menus_modsettings,
audio_bus) pull on first use and push on change.

## Declarations (in source order)

- L22 — `bool GetBool(const char* key, bool defValue)`
- L23 — `int GetInt(const char* key, int defValue)`
- L25 — `void SetBool(const char* key, bool value)`
- L26 — `void SetInt(const char* key, int value)`
- L35 — `bool GetStr(const char* key, char* outBuf, int bufSize)` — false + empty outBuf if key absent
- L36 — `void SetStr(const char* key, const char* value)` — verbatim storage; caller must avoid embedded `=`/newlines (hotkey rebind encoding "vk,altVk,req,forbid" satisfies this)
