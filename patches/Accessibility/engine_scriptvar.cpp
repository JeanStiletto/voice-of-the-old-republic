// Promoted from the throwaway scriptvar_poc — the in-save named-variable
// primitive (player creature's CSWSScriptVarTable at +0x110). See the header
// and docs/llm-docs/persistence-scriptvartable.md for the model and the
// end-to-end save/reload proof.

#include "engine_scriptvar.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "engine_player.h"   // GetPlayerServerCreature
#include "engine_reads.h"    // ReadCExoString
#include "log.h"

namespace acc::engine {
namespace {

// CSWSScriptVarTable accessors + CExoString ctor/dtor (GOG/Steam swkotor.exe;
// GoG bytes match Steam). Addresses from Lane's RE — see the persistence doc.
constexpr uintptr_t kAddrSetString   = 0x0059a8e0;  // __thiscall(table, name, value)
constexpr uintptr_t kAddrGetString   = 0x0059a590;  // __thiscall(table, out, name) -> out
constexpr uintptr_t kAddrSetInt      = 0x0059a6f0;  // __thiscall(table, name, value, journalArg)
constexpr uintptr_t kAddrGetInt      = 0x0059a530;  // __thiscall(table, name) -> int
constexpr uintptr_t kAddrExoCtorCStr = 0x005e5a90;  // CExoString::CExoString(char*)
constexpr uintptr_t kAddrExoDtor     = 0x005e5c20;  // CExoString::~CExoString()

// Named, string-capable CSWSScriptVarTable embedded in CSWSObject at +0x100.
// (Ghidra's struct MISLABELS the fields: the symbol "script_var_table_2" at
// +0x110 is actually a CSWVarTable — the fixed, index-keyed, NON-string table.
// CSWSObject::SaveObjectState/LoadObjectState @0x004cec50/0x004d1cf0 prove the
// real CSWSScriptVarTable is the +0x100 field — they call
// CSWSScriptVarTable::Save/LoadVarTable on &this->field54_0x100 and
// CSWVarTable::Save/LoadVarTable on &this->script_var_table_2. Writing through
// +0x110 with the string accessors derefs a bogus array pointer and faults.)
constexpr uintptr_t kScriptVarTableOffset = 0x100;

typedef void  (__thiscall* PFN_SetString)(void*, void*, void*);
// GetString returns CExoString by value → the hidden out-buffer pointer is the
// FIRST stack arg, `name` second (returned in EAX). Declared exactly so the
// call doesn't return caller-frame garbage (see the persistence doc's
// calling-convention trap).
typedef void* (__thiscall* PFN_GetString)(void*, void*, void*);
typedef void  (__thiscall* PFN_SetInt)(void*, void*, int, int);
typedef int   (__thiscall* PFN_GetInt)(void*, void*);
typedef void* (__thiscall* PFN_ExoCtorCStr)(void*, const char*);
typedef void  (__thiscall* PFN_ExoDtor)(void*);

// Engine CExoString layout: { char* c_string; int length } (8 bytes). POD so
// it is legal as a local inside an SEH (__try) frame.
struct ExoStr { char* p; int len; };

void ExoInit(ExoStr* s, const char* cstr) {
    s->p = nullptr;
    s->len = 0;
    reinterpret_cast<PFN_ExoCtorCStr>(kAddrExoCtorCStr)(s, cstr);
}

void ExoFree(ExoStr* s) {
    reinterpret_cast<PFN_ExoDtor>(kAddrExoDtor)(s);
}

// creature+0x100, or nullptr when no server-side player creature is loaded.
void* PlayerVarTable() {
    void* creature = GetPlayerServerCreature();
    if (!creature) return nullptr;
    return reinterpret_cast<unsigned char*>(creature) + kScriptVarTableOffset;
}

}  // namespace

int GetPlayerVarInt(const char* name, int fallback) {
    void* table = PlayerVarTable();
    if (!table || !name) return fallback;
    __try {
        ExoStr n; ExoInit(&n, name);
        int v = reinterpret_cast<PFN_GetInt>(kAddrGetInt)(table, &n);
        ExoFree(&n);
        return v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

bool SetPlayerVarInt(const char* name, int value) {
    void* table = PlayerVarTable();
    if (!table || !name) return false;
    __try {
        ExoStr n; ExoInit(&n, name);
        // journalArg is ignored for non-"NW_JOURNAL" names; pass 0.
        reinterpret_cast<PFN_SetInt>(kAddrSetInt)(table, &n, value, 0);
        ExoFree(&n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetPlayerVarString(const char* name, char* outBuf, size_t bufSize) {
    if (outBuf && bufSize > 0) outBuf[0] = '\0';
    void* table = PlayerVarTable();
    if (!table || !name || !outBuf || bufSize == 0) return false;
    __try {
        ExoStr n; ExoInit(&n, name);
        // `out` starts as a valid empty CExoString; GetString's internal
        // operator= frees this (no-op for {null,0}) before copying. We then
        // free the engine-allocated buffer with the engine's own dtor (no heap
        // mismatch) — the proven scriptvar_poc recipe.
        ExoStr out = {nullptr, 0};
        reinterpret_cast<PFN_GetString>(kAddrGetString)(table, &out, &n);
        bool ok = ReadCExoString(&out, 0, outBuf, bufSize);
        ExoFree(&out);
        ExoFree(&n);
        return ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (outBuf && bufSize > 0) outBuf[0] = '\0';
        return false;
    }
}

bool SetPlayerVarString(const char* name, const char* value) {
    void* table = PlayerVarTable();
    if (!table || !name || !value) return false;
    __try {
        ExoStr n; ExoInit(&n, name);
        ExoStr v; ExoInit(&v, value);
        reinterpret_cast<PFN_SetString>(kAddrSetString)(table, &n, &v);
        ExoFree(&v);
        ExoFree(&n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


}  // namespace acc::engine
