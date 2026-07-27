# engine_scriptvar.h (43 lines)

Public contract for the player-creature named-variable persistence primitive
(see engine_scriptvar.cpp). Any mod state needing to survive save/reload
without a sidecar file can use this — the engine serializes the table itself.
Variables are keyed by (name, type) together; int "FOO" and string "FOO" are
independent entries.

## Declarations (in source order)

- L21 — `namespace acc::engine`
- L27 — `int GetPlayerVarInt(const char* name, int fallback = 0)`
  note: absent and stored-zero both read as 0 — use a string var or sentinel to distinguish.
- L31 — `bool SetPlayerVarInt(const char* name, int value)`
- L37 — `bool GetPlayerVarString(const char* name, char* outBuf, size_t bufSize)`
  note: returns false for both absent and empty-string values (both read as "").
- L41 — `bool SetPlayerVarString(const char* name, const char* value)`
