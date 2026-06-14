// In-save named-variable persistence on the player creature's
// CSWSScriptVarTable (CSWSObject +0x110). The reusable storage primitive for
// any mod state that must survive save/reload WITHOUT a sidecar file: the
// engine serializes this table into the .sav via CSWSObject::SaveObjectState
// and repopulates it on load, so mutating it in memory is enough — no save
// hook. Full RE reference + addresses: docs/llm-docs/persistence-scriptvartable.md.
//
// All calls reach the SERVER-side player creature (GetPlayerServerCreature)
// and operate on creature+0x110. Every call is SEH-guarded and degrades to a
// safe default (false / fallback / "") when no creature is loaded (main menu,
// pre-chargen) or any accessor faults — callers never need to gate on game
// state themselves.
//
// Variable identity: keyed by (name AND type) together. An int "FOO" and a
// string "FOO" are independent entries; this mirrors the engine.

#pragma once

#include <cstddef>

namespace acc::engine {

// Read a named int var. Returns `fallback` (default 0) when the var is absent,
// no creature is loaded, or any fault. Absent and stored-zero are
// indistinguishable (engine GetInt yields 0 for both) — use a string var or a
// sentinel value if you must tell them apart.
int  GetPlayerVarInt(const char* name, int fallback = 0);

// Write a named int var. True on success; false when no creature is loaded or
// any accessor faults.
bool SetPlayerVarInt(const char* name, int value);

// Read a named string var into outBuf (always NUL-terminated on entry).
// Returns true iff a non-empty value was read; false on absent/empty, no
// creature, or fault. An absent var reads as the empty string (engine
// semantics), reported here as false with outBuf == "".
bool GetPlayerVarString(const char* name, char* outBuf, size_t bufSize);

// Write a named string var (copied into the table-owned CExoString). True on
// success; false when no creature is loaded or any accessor faults.
bool SetPlayerVarString(const char* name, const char* value);

}  // namespace acc::engine
