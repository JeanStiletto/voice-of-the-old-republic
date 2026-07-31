// KOTOR 2 focus announce — the port's first vertical slice.
//
// Why this exists instead of running the KOTOR 1 handler
// -----------------------------------------------------
// OnSetActiveControl's KOTOR 1 body is not portable yet, and the blocker is not
// the handler itself but everything it reaches: IsLoadingSaveGame dispatches
// through an address that resolves to 0 on KOTOR 2, and IdentifyPanel needs the
// CGuiInGame slot table, which is a description of ONE binary's layout and
// almost certainly reordered in a game that added screens KOTOR 1 never had.
// Porting that whole graph before anything can be heard would be a long stretch
// with no feedback in the middle of it.
//
// So KOTOR 2 gets its own deliberately small path that touches ONLY constants
// this port has actually verified against the KOTOR 2 binary:
//
//   kControlIdOffset     0x54  (from CSWGuiControl::Load reading "ID")
//   kLabelTextOffset     0xf0  (label text object 0xd8 + text_params 0x18)
//   kButtonTextOffset    0x178 (button text object 0x160 + text_params 0x18)
//
// No panel classification, no engine calls, no first-sight bookkeeping. It
// reads the focused control's inline caption and speaks it. That is enough to
// answer the only question that matters right now — does our code run inside
// KOTOR 2 and see real GUI state — and every piece it uses is independently
// confirmed, so a wrong result points at exactly one of three numbers.
//
// Deliberately NOT used: ReadLabelText / ReadButtonText. Those go through
// ReadGuiString for the *rendered* string, which needs
// kAurGuiStringCStrOffset — the one offset in the text chain still unverified
// (CAurGUIStringInternal is a class whose vtable slot count did not match).
// Reading the inline CExoString sidesteps it. On KOTOR 1 the inline field is
// often empty, which is why the real extractor prefers gui_string; if KOTOR 2
// turns out to behave the same way, the log will show empty captions and that
// is the signal to go and verify that last offset.
//
// The vtable is logged on purpose: it tells us which control classes actually
// reach focus in KOTOR 2, which is the cheapest way to learn whether the
// class set matches KOTOR 1's.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_game.h"
#include "engine_offsets.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_focus_k2.h"
#include "prism.h"

namespace acc::menus::k2 {

namespace {

// Last caption spoken, so the engine re-firing focus on the same control does
// not repeat it. Same reason menus.cpp dedups: SetActiveControl fires far more
// often than focus actually changes.
char s_lastSpoken[256] = {0};

}  // namespace

void AnnounceFocus(void* panel, void* control) {
    if (!control) return;

    uint32_t vtable = acc::engine::ReadU32(control, 0);
    uint32_t id = acc::engine::ReadU32(control, kControlIdOffset);

    // Try both shapes and take whichever yields text. Distinguishing by vtable
    // would be tidier, but it needs a KOTOR 2 vtable constant per control class
    // and this probe exists precisely to find out which classes turn up.
    char labelText[192] = {0};
    char buttonText[192] = {0};
    bool haveLabel = acc::engine::ReadCExoString(control, kLabelTextOffset,
                                                 labelText, sizeof(labelText));
    bool haveButton = acc::engine::ReadCExoString(control, kButtonTextOffset,
                                                  buttonText, sizeof(buttonText));

    const char* text = nullptr;
    const char* which = "none";
    if (haveLabel && labelText[0]) {
        text = labelText;
        which = "label";
    } else if (haveButton && buttonText[0]) {
        text = buttonText;
        which = "button";
    }

    acclog::Write("K2.Focus",
                  "panel=%p control=%p vtable=%08X id=%d src=%s text=\"%s\"",
                  panel, control, vtable, static_cast<int>(id), which,
                  text ? text : "");

    if (!text || !text[0]) return;
    if (strncmp(s_lastSpoken, text, sizeof(s_lastSpoken)) == 0) return;
    strncpy_s(s_lastSpoken, text, _TRUNCATE);
    prism::Speak(text, /*interrupt=*/false);
}

}  // namespace acc::menus::k2

// CSWGuiPanel::SetActiveControl @0x0040EC00, hooked at 0x0040EC09 — after the
// prologue has stored `this`, so both arguments are already in the frame.
//
// KOTOR 2's GUI code is compiled UNOPTIMISED: it keeps a real frame pointer and
// reloads `this` from [EBP-8] at every use, where KOTOR 1's optimised build
// holds it in a register (which is why the KOTOR 1 hook takes EDI/ESI
// mid-function). So this hook takes EBP and does the frame arithmetic itself:
//
//   [EBP-8] = this        (CSWGuiPanel*)
//   [EBP+8] = param_1     (CSWGuiControl*, the newly active control)
//
// Passing EBP rather than declaring two stack parameters is deliberate:
// KPatchManager's esp+X parameter source emits LEA instead of MOV and hands
// back addresses rather than values (docs/upstream-prs.md PR-2), so any hook
// that needs frame-relative arguments must read them itself.
extern "C" void __cdecl OnSetActiveControlK2(void* ebp) {
    if (!acc::game::IsKotor2()) return;   // belt and braces; the hook is K2-only
    if (!ebp) return;

    void* panel = nullptr;
    void* control = nullptr;
    __try {
        panel = *reinterpret_cast<void**>(static_cast<char*>(ebp) - 8);
        control = *reinterpret_cast<void**>(static_cast<char*>(ebp) + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    acc::menus::k2::AnnounceFocus(panel, control);
}
