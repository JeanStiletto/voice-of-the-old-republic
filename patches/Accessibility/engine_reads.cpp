#include "engine_reads.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_player.h"  // kAddrAppManagerPtr
#include "log.h"

namespace acc::engine {

bool ReadControlNameFields(void* control, const char*& outTip,
                           uint32_t& outTipLen, int& outId) {
    // Always initialise the outs: callers (OnHandleFocusChange) log them
    // unconditionally, so an early return must not leave them garbage.
    outTip    = nullptr;
    outTipLen = 0;
    outId     = 0;
    if (!control) return false;
    // SEH-guarded like CallDowncast below. This is called straight from the
    // OnHandleFocusChange detour, which fires during engine control
    // teardown - exactly the window where `control` is stale rather than
    // null, which a null check cannot catch.
    __try {
        auto* base = reinterpret_cast<unsigned char*>(control);
        outTip    = *reinterpret_cast<const char**>(base + 0x28);
        outTipLen = *reinterpret_cast<uint32_t*>   (base + 0x2c);
        outId     = *reinterpret_cast<int*>        (base + kControlIdOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outTip    = nullptr;
        outTipLen = 0;
        outId     = 0;
        return false;
    }
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

// SEH-guarded single-field reads for possibly-dead engine pointers. A
// non-null control/panel pointer is NOT proof of life (KOTOR 2 tears panels
// down on paths KOTOR 1 does not — the GetControlCenter / FocusProbe crash
// class), so any read through a pointer the engine may have freed goes
// through one of these instead of a raw dereference.
bool TryReadU32(void* base, size_t offset, uint32_t* out) {
    if (!base || !out) return false;
    __try {
        *out = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(base) + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadU16(void* base, size_t offset, uint16_t* out) {
    if (!base || !out) return false;
    __try {
        *out = *reinterpret_cast<uint16_t*>(
            reinterpret_cast<unsigned char*>(base) + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadPtr(void* base, size_t offset, void** out) {
    if (!base || !out) return false;
    __try {
        *out = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(base) + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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

namespace {

// True iff buf looks like real localised text rather than uninitialised
// memory. The action-bar / target-action / radial column buttons leave
// their CSWGuiControl.tooltip_string slot at +0x28 uninitialised
// (engine renders these buttons via a separate dynamic-text path, so the
// .gui-time CExoString never gets written). What we observed in practice
// is a non-null literal pointer + a small length (3 bytes) yielding
// CP1252 control-range bytes 0x80..0x9F — code points the engine never
// emits in any localised UI string. Rejecting those lets the caller fall
// back to the proper "Keine Beschreibung verfügbar" cue.
bool LooksLikeReadableText(const char* buf, size_t len) {
    if (!buf || len == 0) return false;
    size_t printable = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(buf[i]);
        // ASCII printable + extended Latin range (covers German umlauts
        // 0xE4/0xF6/0xFC/0xDF, accented French/Spanish/Italian, etc.).
        // Excludes 0x00..0x1F (controls), 0x7F (DEL), and 0x80..0x9F
        // (CP1252 control block — never used in localised UI strings).
        if ((c >= 0x20 && c < 0x7F) || c >= 0xA0) {
            ++printable;
        }
    }
    // Require at least one printable byte AND a majority of printable
    // bytes. Short all-garbage strings (the 3-byte case we hit) get
    // rejected; legitimate short tooltips like "OK" keep working.
    return printable > 0 && printable * 2 >= len;
}

}  // namespace

bool ReadControlTooltip(void* control, char* outBuf, size_t bufSize) {
    if (!control || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';

    // Bound the parent-walk so a malformed/cyclic parent chain can't spin
    // us forever. The deepest in-game panel hierarchy we've observed is
    // ~6 levels (panel → row → embedded button → text); 8 is generous.
    void*    cur   = control;
    void*    last  = nullptr;
    int      hops  = 0;
    while (cur && cur != last && hops < 8) {
        last = cur;
        ++hops;

        uint32_t strref     = 0;
        const char* literal = nullptr;
        uint32_t literalLen = 0;
        void*    parent     = nullptr;

        __try {
            auto* base = reinterpret_cast<unsigned char*>(cur);
            strref  = *reinterpret_cast<uint32_t*>(base + kControlTooltipStrRefOffset);
            literal = *reinterpret_cast<const char**>(base + kControlTooltipStringOffset);
            literalLen = *reinterpret_cast<uint32_t*>(
                base + kControlTooltipStringOffset + 4);
            parent  = *reinterpret_cast<void**>(base + kControlParentOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        // 1. Strref takes priority (engine decompile: when field4_0x24
        //    is non-zero AND a strref lookup succeeds, that wins over
        //    the literal).
        if (strref != 0 && strref != 0xFFFFFFFF) {
            if (LookupTlk(strref, outBuf, bufSize) && outBuf[0]) {
                return true;
            }
        }

        // 2. Literal tooltip_string. Validate it looks like real text —
        //    action-bar / target-action / radial column buttons leave
        //    this slot uninitialised and the engine never wipes it on
        //    .gui load, so a stale non-null pointer + small length can
        //    return CP1252 control-range garbage. Drop garbage and keep
        //    bubbling so the caller's "no tooltip" fallback fires.
        if (literal && literalLen > 0 && literalLen < bufSize) {
            __try {
                memcpy(outBuf, literal, literalLen);
                outBuf[literalLen] = '\0';
                if (LooksLikeReadableText(outBuf, literalLen)) {
                    return true;
                }
                outBuf[0] = '\0';
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                outBuf[0] = '\0';
                // Fall through to parent walk — corrupt literal pointer.
            }
        }

        // 3. Bubble up to parent and retry (engine recurses via
        //    parent_control->vtable->DisplayToolTip).
        cur = parent;
    }

    return false;
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

bool ReadLabelText(void* label, char* outBuf, size_t bufSize) {
    if (!label || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    __try {
        if (ReadGuiString(label, kLabelGuiStringPtrOffset,
                          outBuf, bufSize) && outBuf[0] != '\0') {
            return true;
        }
        if (ExtractTextOrStrRefIndirect(
                label, kLabelTextOffset, kLabelStrRefOffset,
                kLabelTextObjectOffset, outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
    return false;
}

bool ReadButtonText(void* button, char* outBuf, size_t bufSize) {
    if (!button || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    __try {
        if (ReadGuiString(button, kButtonGuiStringPtrOffset,
                          outBuf, bufSize) && outBuf[0] != '\0') {
            return true;
        }
        if (ExtractTextOrStrRefIndirect(
                button, kButtonTextOffset, kButtonStrRefOffset,
                kButtonTextObjectOffset, outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
    return false;
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
        uintptr_t v = reinterpret_cast<uintptr_t>(vt);
        // CSWGuiSaveGameEditBox (save-name popup) is a subclass with its own
        // vtable but the same struct layout — accept it so its focus-enter
        // announce + field reads go through the same path as the plain editbox.
        return v == kVtableEditbox || v == kVtableSaveGameEditbox;
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
