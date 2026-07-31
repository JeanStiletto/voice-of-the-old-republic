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
// often empty, which is why the real extractor prefers gui_string — and the
// first working run answered the open question: KOTOR 2 DOES populate it on
// buttons ("Neues Spiel" / "Spiel laden" / "Filmsequenzen" all read straight
// out of it). So the unverified gui_string offset does not block text
// extraction here, which is a real divergence from KOTOR 1.
//
// The vtable is logged on purpose: it tells us which control classes actually
// reach focus in KOTOR 2. So far the main menu produces only CSWGuiButton
// (vtable 00987A1C), matching the RTTI scan exactly.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_game.h"
#include "engine_offsets.h"
#include "engine_rebase.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_focus_k2.h"
#include "prism.h"

// Defined in core_dllmain.cpp, and forward-declared per TU the same way
// menus.cpp / menus_dispatch.cpp / transitions.cpp do it.
//
// Prism is loaded LAZILY — DllMain cannot do it (loader lock), so the first
// hook to fire is what initialises the screen reader bridge. Every KOTOR 1
// handler calls this before speaking. The first KOTOR 2 run proved why it
// matters: the focus hook read every caption correctly and spoke none of them,
// because this call was missing and prism::Speak had nothing to speak through.
void EnsurePrismInitialized();

namespace acc::menus::k2 {

namespace {

// CSWGuiControl::HandleFocusChange. It calls SetActiveControl itself, so it
// arrives at our hook looking like a genuine focus change when it is not — and
// this is a KNOWN engine quirk, not a KOTOR 2 novelty: KOTOR 1's notes already
// record that HandleFocusChange "fires twice and reports the wrong control",
// which is precisely why SetActiveControl is the canonical signal there.
//
// Observed on the first speaking KOTOR 2 build: every arrow-key navigation
// produced two events, the real one from CSWGuiNavigable::HandleInputEvent and
// a second from here that dragged focus back to one fixed button, so the menu
// read "Neues Spiel, Spiel laden, Filmsequenzen, Spiel laden".
//
// The KOTOR 1 address is corroborated independently: hooks.toml hooks
// HandleFocusChange at 0x41896b, i.e. this function +0xb.
const uintptr_t kAddrControlHandleFocusChange =
    acc::addr::Pick(0x00418960, 0x00418FE0);
constexpr size_t kHandleFocusChangeSize = 116;   // K2 body length

// Did this SetActiveControl originate inside HandleFocusChange?
bool CallerIsFocusChange(void* caller) {
    if (!caller || !acc::addr::Ok(kAddrControlHandleFocusChange)) return false;
    uintptr_t va = reinterpret_cast<uintptr_t>(caller);
    return va >= kAddrControlHandleFocusChange &&
           va < kAddrControlHandleFocusChange + kHandleFocusChangeSize;
}

// Last caption spoken, so the engine re-firing focus on the same control does
// not repeat it. Same reason menus.cpp dedups: SetActiveControl fires far more
// often than focus actually changes.
char s_lastSpoken[256] = {0};

}  // namespace

void AnnounceFocus(void* panel, void* control, void* caller) {
    if (!control) return;
    EnsurePrismInitialized();

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

    // `caller` is the return address, i.e. WHICH engine function set focus.
    // Present because the first working run showed a second SetActiveControl
    // firing on one fixed control after every keyboard navigation, and the
    // candidate explanations (mouse hover re-asserting, the panel restoring a
    // default, the engine's own nav running alongside ours) are
    // indistinguishable from the arguments alone. The caller names it outright,
    // and maps back to a function via docs/llm-docs/re/k2/k2-functions.csv.
    acclog::Write("K2.Focus",
                  "panel=%p control=%p vtable=%08X id=%d src=%s caller=%p%s text=\"%s\"",
                  panel, control, vtable, static_cast<int>(id), which, caller,
                  CallerIsFocusChange(caller) ? " [focus-change, suppressed]" : "",
                  text ? text : "");

    // Logged above but never spoken: see CallerIsFocusChange. Keeping the log
    // line means a future change in the engine's focus dance stays visible
    // rather than silently altering what the user hears.
    if (CallerIsFocusChange(caller)) return;

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
    void* caller = nullptr;
    __try {
        panel = *reinterpret_cast<void**>(static_cast<char*>(ebp) - 8);
        control = *reinterpret_cast<void**>(static_cast<char*>(ebp) + 8);
        // Standard frame: [EBP] = saved EBP, [EBP+4] = return address.
        caller = *reinterpret_cast<void**>(static_cast<char*>(ebp) + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    acc::menus::k2::AnnounceFocus(panel, control, caller);
}
