// KOTOR 2 gamepad input — the one place that knows what a pad event looks
// like.
//
// Why this module exists
// ----------------------
// KOTOR 2's Steam/Aspyr build ships working gamepad support, and it delivers
// pad events through the SAME engine input path (and the same InputIndex
// numbering) as the keyboard. `InputIndexName` therefore prints misleading
// keyboard names for pad events, and — worse — every downstream handler in the
// mod is written against the keyboard's logical codes (kInputNav*, kInputEnter*,
// kInputEsc*). Scattering "…or the pad code" into each of those handlers would
// put one piece of knowledge in a dozen places.
//
// So there is exactly ONE seam: TranslateManagerEvent, called as the first
// statement of OnHandleInputEvent. It normalises a pad event into the logical
// code the rest of the mod already understands, or handles it outright when the
// pad has a binding the keyboard does not. Nothing downstream of that call
// knows a controller exists.
//
// KOTOR 1 has no pad path at all (no gamepad preamble in its GUI manager), so
// every entry point here is a no-op on KOTOR 1 — which also means the shared
// keyboard codes (F9..F12, A, B, C, D) can never be misread there.
//
// Engine reference: docs/llm-docs/k2-controller-support.md.
// Work plan + test steps: docs/kotor2-controller-plan.md.

#pragma once

namespace acc::pad {

// ---------------------------------------------------------------------------
// Pre-translation InputIndices the KOTOR 2 gamepad path delivers to
// CSWGuiManager::HandleInputEvent (our OnHandleInputEvent hook, in menus AND
// in the world). Every one of these is ALSO a real bindablekeys.2da row, so
// they are shared numbering, not pad-exclusive — see IsPadEvent for how the
// two are told apart.
// ---------------------------------------------------------------------------
constexpr int kPadDpadLeft   = 47;    // logs as KEYBOARD_F9
constexpr int kPadDpadRight  = 48;    // KEYBOARD_F10
constexpr int kPadDpadUp     = 49;    // KEYBOARD_F11
constexpr int kPadDpadDown   = 50;    // KEYBOARD_F12
constexpr int kPadStickVert  = 51;    // KEYBOARD_A — val -1 up / +1 down / 0 centred
constexpr int kPadStickHoriz = 52;    // KEYBOARD_B — val -1 left / +1 right / 0 centred
constexpr int kPadButtonA    = 0x27;  // the engine's GUI activate code (= F1)
constexpr int kPadButtonB    = 40;    // KEYBOARD_F2
constexpr int kPadButtonX     = 41;   // KEYBOARD_F3
constexpr int kPadButtonY     = 42;   // KEYBOARD_F4
constexpr int kPadShoulderL   = 53;   // KEYBOARD_C
constexpr int kPadShoulderR   = 54;   // KEYBOARD_D
constexpr int kPadBack        = 46;   // KEYBOARD_F8

// NOT pad codes, recorded so nobody reads them as such again. Codes 65 and 66
// arriving at CClientExoAppInternal::HandleInputEvent were once read as the
// left stick's in-world movement ("65 = held direction, 66 = a varying
// companion value"). They are the MOUSE X and Y axes. The refutation is
// arithmetic: every observed 65 value is exactly a MoveMouseToPosition X the
// mod itself issued (1743, 852, 891, 848), and the 66 values step by 87 —
// the main menu's row spacing — because the "analog sweep" was our own chain
// navigation warping the cursor down the button column. They also arrive on
// the title screen, where no world and no player exist.
//   constexpr int kPadMoveDirection = 65;   // <- mouse X. Do not resurrect.
//   constexpr int kPadMoveVarying   = 66;   // <- mouse Y. Do not resurrect.

// ---------------------------------------------------------------------------
// The manager seam
// ---------------------------------------------------------------------------

// What the caller must do with the event after the seam has looked at it.
enum class Verdict {
    // Not a pad event (or not KOTOR 2). Carry on with `code` / `value`
    // untouched — this is the overwhelmingly common answer.
    NotPad,
    // `code` (and, for a stick release, `value`) have been rewritten into the
    // keyboard-logical equivalent. Carry on; every downstream handler now sees
    // a code it already understands.
    Rewritten,
    // Fully handled here — the caller must consume the event (return 1). Used
    // for the in-world pad bindings and for routing into the mod's own
    // in-DLL overlays, neither of which has anything for the engine to do.
    Consumed,
};

// Normalise one CSWGuiManager::HandleInputEvent event. Call as the FIRST
// statement of the handler, before the press-release pairing — the pairing
// tracker has to see the same (rewritten) code on press and on release or a
// later real press gets swallowed.
Verdict TranslateManagerEvent(int& code, int& value);

// True iff `code` could be a pad event on the running build. Used by
// InputIndexName's log annotation, and by the help system to decide whether a
// raw 0x27 is a physical F1 or a pad A.
bool IsPadCode(int code);

// Short label for what a pad-plausible code means on the pad ("D-Pad down",
// "A", "left stick vertical"), or nullptr when the code cannot be a pad event
// on the running build. InputIndexName appends it so a log line reading
// `KEYBOARD_F12 [pad D-Pad down?]` is self-explanatory.
//
// Deliberately a HINT, not a rename: the numbering really is shared with
// bindablekeys.2da rows, so the keyboard name stays the primary one and the
// question mark stays on the annotation.
const char* CodeHint(int code);

// True iff a raw 0x27 arriving at the GUI manager is a PHYSICAL F1 rather than
// the pad's A button. Always true on KOTOR 1 (there is no pad path, so 0x27 is
// unambiguously F1); on KOTOR 2 it asks the OS whether F1 is actually down.
bool IsPhysicalF1();

// ---------------------------------------------------------------------------
// Trigger layer (Phase 1)
// ---------------------------------------------------------------------------
// LT and RT are the only pad inputs the engine never reads: the joystick
// reader never emits the lZ axis, which is where both triggers live on an
// Xbox pad under DirectInput. That also means DirectInput cannot tell them
// apart (they are two halves of one axis and cancel out), so we read them
// through XInput, where bLeftTrigger / bRightTrigger are separate. XInput
// coexists with the engine's grab — the engine holds the pad
// DISCL_BACKGROUND | DISCL_NONEXCLUSIVE.
//
// xinput1_4 / 1_3 / 9_1_0 are resolved at run time via LoadLibrary, never
// linked: a delay-load import that fails to resolve takes the process down
// with 0xC06D007F (see project_prism_backend_delayload_crash).

// Per-tick poll: refreshes trigger state and pad presence. Cheap (one
// XInputGetState on a single slot) and self-gates to KOTOR 2. Call from
// core_tick::Dispatch.
void Tick();

bool LeftTriggerHeld();
bool RightTriggerHeld();

// True once a gamepad has been seen this session — either XInput reports one
// connected, or an unmistakably pad-shaped event has arrived (a stick axis
// carrying val = +/-1, or a D-Pad code; the face and shoulder codes are shared
// with real keyboard rows and do not count). Sticky, and drives the controller
// section of the F1 help list.
bool Connected();

// ---------------------------------------------------------------------------
// Analog movement (Phase 2)
// ---------------------------------------------------------------------------
// The engine's keyboard-only "is the player commanding translation" query
// (engine_keymap::ForwardBackwardKeyHeld) reads VKs, so a stick-driven walk
// holds no key and never satisfies it — which is why a wheeled droid grinding
// a wall under stick control keeps its drive loop sounding.
//
// The answer is the same XInput read the triggers already use, not an engine
// signal. That is deliberate after the first attempt: the engine event pair
// that looked like stick movement turned out to be the mouse axes (see the
// note on codes 65/66 above), and reading the stick directly is both correct
// and immune to that whole class of misreading. The LEFT stick is Move and the
// RIGHT stick is Rotate Camera on the shipped binding chart, so the left
// stick's magnitude IS "the player is commanding translation" — a sharper
// signal than the keyboard heuristic it joins.
//
// True while the left stick is pushed past the dead zone. Sampled once per
// tick by Tick(); false whenever XInput is unavailable or no pad is connected.
bool StickMoving();

// The left stick as a normalised direction, screen convention: x = +1 right,
// y = +1 UP (the axis as XInput reports it, not the map's inverted pixel
// space — the caller flips if it wants pixels). Returns false and leaves the
// outputs untouched when the stick is inside the dead zone.
//
// This is what makes the stick a continuous analog driver rather than four
// synthetic key presses: the map cursor scales its pan by the magnitude the
// player is actually giving it. Same sample StickMoving reads, so the two can
// never disagree.
bool StickVector(float& outX, float& outY);

}  // namespace acc::pad
