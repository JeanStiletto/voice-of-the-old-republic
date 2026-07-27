# mod_settings_store.cpp (154 lines)

Persistent key=value settings file at `<install>\acc_settings.ini`, derived
from `acclog::PatchDir()` by stripping the trailing `\patches` component.
Lazy-loads into an in-memory `std::map` on first access (mutex-guarded),
rewrites the whole file on every Set (file is tiny). Missing file/key/patch-dir
all fall back to caller-supplied defaults rather than erroring.

## Declarations (in source order)

- L18-L21 — statics: `g_mtx`, `g_kv` (std::map<string,string>), `g_loaded`, `g_path[MAX_PATH]`
- L26-L36 — `bool ResolvePath()` — derives `g_path` from `acclog::PatchDir()`; false if patch dir unknown (very early DLL attach)
- L39-L69 — `void EnsureLoaded()` — must hold g_mtx; parses `key=value` lines, skips blank/`#`/`;` comments
- L72-L84 — `void Save()` — must hold g_mtx; full rewrite with header comment
- L88-L98 — `bool GetBool(const char* key, bool defValue)` — accepts 1/true/on/yes and 0/false/off/no
- L100-L110 — `int GetInt(const char* key, int defValue)` — strtol, unparseable → default
- L112-L119 — `void SetBool(const char* key, bool value)`
- L121-L130 — `void SetInt(const char* key, int value)`
- L132-L142 — `bool GetStr(const char* key, char* outBuf, int bufSize)`
- L144-L151 — `void SetStr(const char* key, const char* value)` — stores raw, no escaping
