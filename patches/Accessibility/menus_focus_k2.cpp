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
#include "engine_manager.h"
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

// The shared focus handler, defined in menus.cpp and hooked directly on
// KOTOR 1. KOTOR 2 reaches it through this file instead, because its hook has
// to do frame arithmetic and the cursor warp first — see OnSetActiveControlK2.
// Forward-declared per TU the same way EnsurePrismInitialized is.
extern "C" void __cdecl OnSetActiveControl(void* panel, void* newControl);

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

// Park the engine's cursor on a control, so mouse hover stops fighting the
// keyboard.
//
// The problem this solves: the cursor sits wherever it was left, the manager
// hit-tests at that position and hands the control under it focus via
// HandleFocusChange, and that overrides whatever the arrow keys just selected.
// Focus therefore snaps back to one fixed button after every keypress, and only
// its immediate neighbours are ever reachable.
//
// This is NOT a KOTOR 2 defect — KOTOR 1 has the identical conflict, which is
// exactly why its navigation chain calls MoveMouseToPosition to keep the
// engine's own cursor on the focused control.
//
// This started as a stand-in for MoveMouseToPosition, whose KOTOR 2 address was
// unknown at the time. That address is known now (0x00414230) and swapping it in
// was TRIED AND REVERTED on 2026-07-31 — see the note inside
// WarpCursorToControl. The OS-level warp is the KOTOR 2 answer, not a
// placeholder for it.
//
// The original reasoning still holds for why the writes alone are well-founded:
// KOTOR 1's MoveMouseToPosition begins with exactly
// `this->mouse_x = x; this->mouse_y = y` and KOTOR 2's hover hit-test reads
// exactly those two fields (manager+0, manager+4), so this sets the same state
// the engine sets itself. What KOTOR 2 adds is that setting it is not enough.
//
// Both offsets used here are verified: the control extent is Same(0x4),
// observed in KOTOR 2's own panel hit-test.
// CSWGuiManager's hover routine: re-runs the hit test at the manager's stored
// cursor (manager+0 / manager+4) and updates the hovered control (manager+8).
// KOTOR 2 only — this whole file is K2-only — and it has no KOTOR 1
// counterpart with the same shape: KOTOR 1's HandleMouseMove takes explicit x/y
// arguments, whereas this one reads the stored coordinates, so it is a
// different function rather than the same one relocated.
//
// Needed because writing the coordinates alone changed nothing: the first
// attempt set manager+0/+4 and the log still showed hoveredId pinned at the old
// control on all 51 subsequent events. manager+8 only moves when the hit test
// actually runs, which is precisely the step MoveMouseToPosition performs after
// setting the coordinates.
constexpr uintptr_t kAddrK2ManagerRehover = 0x00413C50;
typedef void(__thiscall* PFN_Rehover)(void*);

// NOTE for anyone extending this: control extents in KOTOR 2 are already in
// SCREEN pixels, not the GUI's virtual 640x480 space. The panel hit-test's
// `x -= (screenWidth - 640) / 2` conversion applies to coordinates arriving
// from elsewhere, NOT to extents — measured live, the three main-menu buttons
// report extents x=1321 with y=792/879/966, which is a real 1920-wide layout.
// An earlier attempt applied that conversion to extent centres and pushed the
// warp target off-screen.

// Re-entrancy latch. We are called from INSIDE SetActiveControl, and the
// re-hover below can drive focus changes that come straight back through this
// same hook. KOTOR 1 avoids this class of problem by deferring such work to the
// next Update tick; KOTOR 2 has no Update hook yet, so a latch is the
// proportionate equivalent — it keeps the recursion one level deep and stops
// the nested event being logged or spoken as if the user had navigated.
bool s_inWarp = false;

void WarpCursorToControl(void* control) {
    if (!control || !acc::addr::Ok(kAddrGuiManagerPtr)) return;
    if (s_inWarp) return;
    s_inWarp = true;
    __try {
        void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
        if (mgr) {
            const int* extent = reinterpret_cast<const int*>(
                static_cast<char*>(control) + kControlExtentOffset);
            int cx = extent[0] + extent[2] / 2;   // left + width/2
            int cy = extent[1] + extent[3] / 2;   // top  + height/2

            // DO NOT replace this with the engine's MoveMouseToPosition.
            //
            // That was tried on 2026-07-31, once its KOTOR 2 address
            // (0x00414230) was established, on the reasoning that KOTOR 1's
            // navigation chain calls exactly that and a known-good engine line
            // should beat a reimplementation. **It regressed immediately**:
            // focus was dragged back to "Spiel laden" after every keypress —
            // precisely the bug this function exists to fix.
            //
            // The address is not wrong; the assumption was. KOTOR 2's
            // CExoInput::SetMousePos does NOT move the OS cursor the way
            // KOTOR 1's does. It only writes the engine's own copy, which the
            // engine then overwrites from the true mouse on the very next frame
            // — the same measured behaviour recorded below, now confirmed a
            // second time from the other direction.
            //
            // So the OS-level warp is not a stand-in for a missing address. It
            // is the KOTOR 2 answer, and the missing address was never why it
            // was written this way.
            //
            // Writing manager+0/+4 alone is useless for the same reason: the
            // field held 1440,899 across every event no matter what was stored.
            // That is why three earlier attempts failed identically.
            //
            // Extents are window-client coordinates, so map to screen first —
            // a no-op in exclusive fullscreen, correct in windowed.
            POINT pt = { cx, cy };
            HWND hwnd = GetActiveWindow();
            if (hwnd) ClientToScreen(hwnd, &pt);
            SetCursorPos(pt.x, pt.y);

            // Keep the manager's copy consistent, then re-hit-test so hovered
            // (manager+8) follows immediately rather than a frame later.
            int* cursor = reinterpret_cast<int*>(mgr);
            cursor[0] = cx;
            cursor[1] = cy;
            reinterpret_cast<PFN_Rehover>(kAddrK2ManagerRehover)(mgr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Nothing to recover; a failed warp just means hover keeps fighting.
    }
    s_inWarp = false;
}

}  // namespace

void AnnounceFocus(void* panel, void* control, void* caller) {
    if (!control) return;
    // Focus changes the re-hover triggers are our own doing, not the user's.
    if (s_inWarp) return;
    EnsurePrismInitialized();

    uint32_t vtable = acc::engine::ReadU32(control, 0);
    uint32_t id = acc::engine::ReadU32(control, kControlIdOffset);

    // The panel's CURRENT active control, read before the engine changes it —
    // we hook at 0x0040EC09, ahead of SetActiveControl's own
    // `if (active != param_1)` test, so we also see calls that change nothing.
    //
    // This distinguishes the two readings of the doubled events that the
    // arguments alone cannot: if prev == the incoming control the second call
    // is a no-op and focus is not really moving, whereas if it differs then
    // focus genuinely snaps to that control after every navigation and the
    // menu only ever leaves from there — which would explain why just its two
    // neighbours are reachable.
    void* prevActive = nullptr;
    uint32_t prevId = 0xFFFFFFFFu;
    if (panel) {
        prevActive = reinterpret_cast<void*>(
            acc::engine::ReadU32(panel, kPanelActiveControlOffset));
        if (prevActive) prevId = acc::engine::ReadU32(prevActive, kControlIdOffset);
    }

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
    // The CSWGuiManager's hovered control (manager+8), i.e. what the mouse is
    // over. KOTOR 2's hover path (FUN_00413c50) hit-tests at the stored cursor
    // and writes the result here.
    //
    // Present to settle why focus snaps back to one fixed control after every
    // keypress. Hover was dismissed earlier because it writes THIS field rather
    // than the panel's active_control — but that only rules out a direct write,
    // not HandleFocusChange propagating it into the panel afterwards. If this
    // reads as the same control the bounce lands on, the cursor is simply
    // parked over that button and the engine keeps reclaiming focus for it,
    // which is the same fight KOTOR 1 has and solves by warping the cursor.
    uint32_t hoveredId = 0xFFFFFFFFu;
    void* hovered = nullptr;
    int curX = -1, curY = -1;
    if (acc::addr::Ok(kAddrGuiManagerPtr)) {
        void* mgr = reinterpret_cast<void*>(
            *reinterpret_cast<uintptr_t*>(kAddrGuiManagerPtr));
        if (mgr) {
            hovered = reinterpret_cast<void*>(acc::engine::ReadU32(mgr, 8));
            if (hovered) hoveredId = acc::engine::ReadU32(hovered, kControlIdOffset);
            curX = static_cast<int>(acc::engine::ReadU32(mgr, 0));
            curY = static_cast<int>(acc::engine::ReadU32(mgr, 4));
        }
    }

    // Where the control actually is, in its own (virtual 640x480) space, so the
    // log shows both halves of the coordinate conversion side by side.
    int ex = static_cast<int>(acc::engine::ReadU32(control, kControlExtentOffset));
    int ey = static_cast<int>(acc::engine::ReadU32(control, kControlExtentOffset + 4));

    bool noop = (prevActive == control);
    acclog::Write("K2.Focus",
                  "prev=%d -> id=%d %s hoveredId=%d cursor=%d,%d extent=%d,%d src=%s caller=%p%s text=\"%s\"",
                  static_cast<int>(prevId), static_cast<int>(id),
                  noop ? "[NO-OP]" : "[change]", static_cast<int>(hoveredId),
                  curX, curY, ex, ey,
                  which, caller,
                  CallerIsFocusChange(caller) ? " [focus-change]" : "",
                  text ? text : "");

    if (noop) return;

    // Keep the engine's cursor on whatever the keyboard just selected, so the
    // next hover hit-test agrees with it instead of dragging focus back.
    //
    // Only on a real navigation: warping on HandleFocusChange's own call would
    // chase the cursor to wherever hover already pointed, which is the state we
    // are trying to correct. Once the cursor follows the keyboard, that call
    // targets the control already active and SetActiveControl's own
    // `if (active != param_1)` test makes it a no-op — so the doubled event
    // disappears on its own, without a caller filter suppressing it.
    //
    // This runs BEFORE the shared handler below, and that order matters: the
    // warp can drive a nested SetActiveControl straight back through this hook,
    // and the s_inWarp latch at the top of this function is what stops the
    // nested event being announced as if the user had navigated. Announcing
    // first would let the nested call arrive mid-announce instead.
    if (!CallerIsFocusChange(caller)) WarpCursorToControl(control);

    // Hand off to the REAL handler — the same one KOTOR 1 runs.
    //
    // Everything above this line is KOTOR 2-specific and stays: the frame-based
    // argument read, the no-op detection (we hook ahead of the engine's own
    // `active != param_1` test, so we see calls that change nothing), the caller
    // filter, and the cursor warp. Everything BELOW the announce — panel
    // classification, first-sight capture, panel titles, the full extractor
    // ladder — is shared logic that has no business being reimplemented here.
    //
    // The minimal caption reader this file used to end with is gone. It existed
    // because the shared handler's chain was not ported; it is now, so keeping a
    // second speech path would just be two things to maintain and a double
    // announce. The text read above survives only to enrich the K2.Focus
    // diagnostic line.
    OnSetActiveControl(panel, control);
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
