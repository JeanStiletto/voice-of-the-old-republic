# engine_scriptvar.cpp (138 lines)

Implements in-save named-variable persistence on the player's server-side
CSWSScriptVarTable (CSWSObject +0x100 — NOT the Ghidra-mislabelled +0x110,
which is actually a different, non-string CSWVarTable; see the header and
docs/llm-docs/persistence-scriptvartable.md). Every accessor is SEH-guarded
and degrades to a safe fallback when no player creature is loaded. Talks to
engine_player (GetPlayerServerCreature) and engine_reads (ReadCExoString).

## Declarations (in source order)

- L17 — `namespace acc::engine`
- L22-27 — `kAddrSetString/GetString/SetInt/GetInt/ExoCtorCStr/ExoDtor` — engine addrs
  note: GoG bytes match Steam; addresses from Lane's RE per persistence doc.
- L37 — `constexpr uintptr_t kScriptVarTableOffset = 0x100`
  note: the REAL CSWSScriptVarTable; +0x110 (Ghidra's label) faults on write.
- L39-48 — `PFN_SetString/GetString/SetInt/GetInt/ExoCtorCStr/ExoDtor` typedefs
  note: GetString returns CExoString by value — hidden out-ptr is the first stack arg, not ECX+1; must be declared exactly or reads caller garbage.
- L52 — `struct ExoStr { char* p; int len; }` — POD engine CExoString mirror
- L54 — `void ExoInit(ExoStr*, const char*)`
- L60 — `void ExoFree(ExoStr*)`
- L65 — `void* PlayerVarTable()` — creature+0x100 or nullptr
- L73 — `int GetPlayerVarInt(const char*, int fallback)`
- L86 — `bool SetPlayerVarInt(const char*, int)`
- L100 — `bool GetPlayerVarString(const char*, char* outBuf, size_t)`
  note: out starts as valid empty CExoString; engine's own dtor frees it, avoiding a cross-DLL heap mismatch.
- L122 — `bool SetPlayerVarString(const char*, const char*)`
