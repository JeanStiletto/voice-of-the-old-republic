#include "engine_reads.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"

namespace acc::engine {

bool ReadControlNameFields(void* control, const char*& outTip,
                           uint32_t& outTipLen, int& outId) {
    auto* base = reinterpret_cast<unsigned char*>(control);
    outTip    = *reinterpret_cast<const char**>(base + 0x28);
    outTipLen = *reinterpret_cast<uint32_t*>   (base + 0x2c);
    outId     = *reinterpret_cast<int*>        (base + 0x50);
    return outTip && outTipLen > 0;
}

typedef void* (__thiscall* PFN_Downcast)(void* this_);
void* CallDowncast(void* control, int vtableIndex) {
    if (!control) return nullptr;
    __try {
        void** vtable = *reinterpret_cast<void***>(control);
        if (!vtable) return nullptr;
        auto fn = reinterpret_cast<PFN_Downcast>(vtable[vtableIndex]);
        if (!fn) return nullptr;
        return fn(control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadCExoString(void* base, size_t offset,
                    char* outBuf, size_t bufSize) {
    auto* p = reinterpret_cast<unsigned char*>(base) + offset;
    const char* s   = *reinterpret_cast<const char**>(p);
    uint32_t    len = *reinterpret_cast<uint32_t*>(p + 4);
    if (!s || len == 0 || len >= bufSize) return false;
    memcpy(outBuf, s, len);
    outBuf[len] = '\0';
    return true;
}

uint32_t ReadU32(void* base, size_t offset) {
    return *reinterpret_cast<uint32_t*>(
        reinterpret_cast<unsigned char*>(base) + offset);
}

// Out-arg: the engine copy-constructs `out` from a stack-local CExoString,
// allocating a fresh c_string via its own CRT. We copy the string into our
// caller's buffer, then deliberately leak `tmp.c_string` — calling the
// engine's CExoString destructor from our DLL would risk heap mismatch, and
// focus events are low-frequency enough that the leak is negligible across
// a session.
//
// Sanity bounds: we reject strref values that look invalid (-1, >0x100000)
// before invoking the engine, to reduce the rate of expected exceptions.
//
// The engine's GetSimpleString has its own SEH frame (it can raise on
// out-of-range / corrupt indices). A previous attempt to call it from inside
// a hook handler caused our patch to go silent partway through Options —
// presumably the engine's exception unwound through our trampoline and the
// framework disabled the hook on subsequent fires. We now wrap the call in
// __try/__except so any raised exception is contained.
bool LookupTlk(uint32_t strref, char* outBuf, size_t bufSize) {
    if (strref == 0 || strref == 0xFFFFFFFF) return false;
    if (strref > 0x100000) return false;  // KOTOR's TLK is well below this

    void* tlk = *reinterpret_cast<void**>(kAddrTlkTablePtr);
    if (!tlk) return false;

    auto fn = reinterpret_cast<PFN_GetSimpleString>(kAddrGetSimpleString);
    CExoString tmp = {nullptr, 0};
    bool ok = false;
    __try {
        fn(tlk, &tmp, strref);
        if (tmp.c_string && tmp.length > 0 && tmp.length < bufSize) {
            memcpy(outBuf, tmp.c_string, tmp.length);
            outBuf[tmp.length] = '\0';
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads", "TLK lookup raised SEH exception for strref=%u", strref);
        ok = false;
    }
    return ok;
}

bool ExtractTextOrStrRef(void* control,
                         size_t cexoOffset, size_t strRefOffset,
                         char* outBuf, size_t bufSize) {
    if (ReadCExoString(control, cexoOffset, outBuf, bufSize)) return true;
    uint32_t strref = ReadU32(control, strRefOffset);
    return LookupTlk(strref, outBuf, bufSize);
}

bool ReadGuiString(void* control, size_t guiStringPtrOffset,
                   char* outBuf, size_t bufSize) {
    if (!control || bufSize < 2) return false;
    bool got = false;
    __try {
        void* guiString = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(control) + guiStringPtrOffset);
        if (!guiString) return false;
        uintptr_t gsVtable = *reinterpret_cast<uintptr_t*>(guiString);
        if (gsVtable != kVtableCAurGUIStringInternal) return false;
        char* str = *reinterpret_cast<char**>(
            reinterpret_cast<unsigned char*>(guiString) + kAurGuiStringCStrOffset);
        if (!str) return false;
        size_t len = 0;
        while (len < bufSize - 1 && str[len] != '\0') ++len;
        if (len == 0) return false;
        memcpy(outBuf, str, len);
        outBuf[len] = '\0';
        got = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads", "ReadGuiString SEH for control=%p offset=0x%x",
                      control, (unsigned)guiStringPtrOffset);
        got = false;
    }
    return got;
}

bool ExtractTextOrStrRefIndirect(void* control,
                                 size_t cexoOffset, size_t strRefOffset,
                                 size_t textObjectOffset,
                                 char* outBuf, size_t bufSize) {
    // gui_string offset for label and button differ by inline CSWGuiText
    // start offset; derive from cexoOffset to avoid threading another
    // parameter through every call site (gui_string ptr is at
    // text.+0x14 = (cexo - 0x18) + 0x14 = cexo - 4).
    size_t guiStringPtrOffset = cexoOffset - 4;
    if (ReadGuiString(control, guiStringPtrOffset, outBuf, bufSize)) {
        return true;
    }
    if (ExtractTextOrStrRef(control, cexoOffset, strRefOffset, outBuf, bufSize)) {
        return true;
    }
    bool got = false;
    __try {
        void* textObj = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(control) + textObjectOffset);
        if (textObj) {
            got = ExtractTextOrStrRef(textObj,
                                      kTextObjectTextOffset,
                                      kTextObjectStrRefOffset,
                                      outBuf, bufSize);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Engine.Reads", "text_object indirection SEH for control=%p "
                      "(textObjectOffset=0x%x)", control, (unsigned)textObjectOffset);
        got = false;
    }
    return got;
}

bool IsToggle(void* control) {
    return CallDowncast(control, kVtableAsButtonToggle) != nullptr;
}

// Vtable-identity predicates run from per-tick menu monitors. A null-check
// alone isn't enough: panel-teardown windows (e.g. modal close → area load)
// can leave a freed-but-non-null control pointer in cached monitor state
// for one extra Dispatch() tick. Crash analysed 2026-05-11 (dump
// swkotor.exe.14028.dmp): IsSlider faulted at the vtable read on a freed
// PartySelection OK button right after `SubScreen.Status new_status=4`.
// SEH-guard the dereference so a stale pointer returns false (same as
// a real type mismatch) instead of access-violation-ing the process.
// CallDowncast above uses the same pattern for the IsToggle path.
bool IsSlider(void* control) {
    if (!control) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(control);
        return reinterpret_cast<uintptr_t>(vt) == kVtableSlider;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsListBox(void* control) {
    if (!control) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(control);
        return reinterpret_cast<uintptr_t>(vt) == kVtableListBox;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsEditbox(void* control) {
    if (!control) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(control);
        return reinterpret_cast<uintptr_t>(vt) == kVtableEditbox;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadToggleState(void* toggle) {
    return (ReadU32(toggle, kButtonToggleStateOffset) & 1u) != 0;
}

void DumpControlVtable(void* control, char* out, size_t outSize) {
    void** vtable = *reinterpret_cast<void***>(control);
    if (!vtable) {
        snprintf(out, outSize, "vtable=NULL");
        return;
    }
    snprintf(out, outSize,
             "vtable=%p [0]=%p [4]=%p [20]=%p [22]=%p",
             vtable, vtable[0], vtable[4], vtable[20], vtable[22]);
}

}  // namespace acc::engine
