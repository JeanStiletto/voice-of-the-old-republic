#include "cycle_input.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

// user32.lib for GetAsyncKeyState / GetForegroundWindow /
// GetWindowThreadProcessId. The patch build script (create-patch.bat) links
// only sqlite3 by default; pragma keeps the link config local to the TU
// that needs it.
#pragma comment(lib, "user32.lib")

#include "audio_bus.h"
#include "audio_cues.h"
#include "cycle_state.h"
#include "engine_area.h"
#include "engine_input.h"
#include "engine_player.h"
#include "filter_objects.h"
#include "guidance_autowalk.h"
#include "log.h"
#include "peek_description.h"
#include "strings.h"
#include "tolk.h"

namespace acc::cycle_input {

namespace {

// Engine-side shift flag, mutated by manager-level shift up/down events.
// The manager doesn't bake shift into a modifier flag — DirectInput delivers
// each key as its own event — so we track it ourselves. The Win32 polling
// path queries GetAsyncKeyState directly (no need to share this flag).
bool g_engineShiftHeld = false;

// Clock-position relative to player facing, returning 1..12 with 12 =
// directly ahead, 3 = to the right, 6 = directly behind, 9 = to the left.
//
// Player yaw frame: degrees CCW from +X (engine convention via
// GetPlayerYawDegrees). World-frame angle of the (object - player) vector
// is atan2(dy, dx). Their difference is the relative bearing CCW from
// "ahead"; clocks tick CW, so we negate before bucketing into 30° slots.
//
// Returns 12 for the directly-ahead bucket (which would otherwise be 0).
int ClockPosition(float playerYawDeg, float dx, float dy) {
    constexpr float kRadToDeg = 57.29577951308232f;
    float worldAngle = std::atan2(dy, dx) * kRadToDeg;
    float relCcw = worldAngle - playerYawDeg;
    float clockDeg = -relCcw;  // flip to clockwise from +ahead
    while (clockDeg < 0.0f)    clockDeg += 360.0f;
    while (clockDeg >= 360.0f) clockDeg -= 360.0f;
    int hour = static_cast<int>(clockDeg / 30.0f + 0.5f) % 12;
    return hour == 0 ? 12 : hour;
}

// Map a CycleCategory to its strings::Id pair (singular name + empty
// message) and its 3D audio cue. Centralises the table so OnCycleItem /
// OnCycleCategory don't each carry duplicate switches.
struct CategoryBindings {
    acc::strings::Id  name;    // singular for prefix / fallback
    acc::strings::Id  empty;   // full pre-formatted "no X in range"
    acc::audio::NavCue cue;    // 3D positional cue played at the focused
                               // object's world position before speech
};

CategoryBindings BindingsFor(acc::filter::CycleCategory c) {
    using S = acc::strings::Id;
    using N = acc::audio::NavCue;
    using C = acc::filter::CycleCategory;
    switch (c) {
        case C::Door:
            return {S::CategoryDoor,       S::EmptyDoors,       N::Door};
        case C::Npc:
            return {S::CategoryNpc,        S::EmptyNpcs,        N::NpcCreature};
        case C::Container:
            return {S::CategoryContainer,  S::EmptyContainers,  N::ContainerPlaceable};
        case C::Item:
            return {S::CategoryItem,       S::EmptyItems,       N::Item};
        case C::Landmark:
            return {S::CategoryLandmark,   S::EmptyLandmarks,   N::Landmark};
        case C::Transition:
            return {S::CategoryTransition, S::EmptyTransitions, N::TransitionExit};
        case C::Count_:
            break;
    }
    return {S::CategoryItem, S::EmptyAll, N::Item};  // unreachable safety net
}

// Format the "{name}, {clock} o'clock, {metres} metres" payload (or the
// no-clock variant) into outBuf. Returns the bytes written (excluding
// terminator). Bound to acc::strings, so wording / locale lives there.
int FormatItemPayload(const char* name, bool haveYaw, int clock,
                      int metres, char* outBuf, size_t outBufSize) {
    if (metres < 1) metres = 1;
    const char* fmt = acc::strings::Get(haveYaw
        ? acc::strings::Id::FmtAnnounceWithClock
        : acc::strings::Id::FmtAnnounceNoClock);
    int n = haveYaw
        ? std::snprintf(outBuf, outBufSize, fmt, name, clock, metres)
        : std::snprintf(outBuf, outBufSize, fmt, name, metres);
    return n < 0 ? 0 : n;
}

// Speak the locked Pillar 4 payload for whatever the cycle state
// currently focuses. `categoryPrefix` non-null wraps the item with
// "{category}. {item}" via the FmtCategoryItem template; null speaks
// just the item. Distance rounds to whole metres; sub-1m floors to 1
// (avoids "0 metres"). When player yaw is unavailable (degenerate
// facing during spawn / mid-load), the clock segment is dropped.
void AnnounceCurrent(const acc::cycle::CategoryListing& listing,
                     const char* categoryPrefix) {
    auto& s = acc::cycle::GetState();
    auto bindings = BindingsFor(s.category);

    if (!s.focusedObj || s.focusedIndex < 0 ||
        s.focusedIndex >= listing.count) {
        // Empty state — speak the localized "no X in range" phrase. The
        // category-prefix wrapper is intentionally NOT applied here:
        // EmptyDoors etc. are full sentences, not item names. No 3D cue
        // either, since there's no object to localise spatially.
        const char* msg = acc::strings::Get(bindings.empty);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle: -> [%s]", msg);
        return;
    }

    // 3D positional cue at the focused object's world position. Per the
    // cross-pillar audio vocabulary in audio_cues.h, each Pillar 4
    // category has a designated short cue; the engine's Miles 3D pipeline
    // pans + attenuates it relative to the camera-anchored listener (Q8 +
    // Phase 1 lay-off 4 gate verification). Fired before TTS so the user
    // hears spatial direction first, then the localized name + clock +
    // distance reinforcement.
    const Vector& objPos = listing.positions[s.focusedIndex];
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(bindings.cue), objPos);

    char name[128] = "";
    if (!acc::engine::GetObjectName(s.focusedObj, name, sizeof(name)) ||
        name[0] == '\0') {
        // Fall back to the localized category name — at least the user
        // knows the kind even if no localized name resolves.
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(bindings.name));
    }

    Vector playerPos;
    float yaw = 0.0f;
    bool haveYaw = acc::engine::GetPlayerPosition(playerPos) &&
                   acc::engine::GetPlayerYawDegrees(yaw);
    int clock = 12;
    if (haveYaw) {
        clock = ClockPosition(yaw, objPos.x - playerPos.x,
                              objPos.y - playerPos.y);
    }
    int metres = static_cast<int>(listing.distances[s.focusedIndex] + 0.5f);

    char itemMsg[192];
    FormatItemPayload(name, haveYaw, clock, metres, itemMsg, sizeof(itemMsg));

    char fullMsg[256];
    if (categoryPrefix && categoryPrefix[0] != '\0') {
        std::snprintf(fullMsg, sizeof(fullMsg),
                      acc::strings::Get(acc::strings::Id::FmtCategoryItem),
                      categoryPrefix, itemMsg);
    } else {
        std::snprintf(fullMsg, sizeof(fullMsg), "%s", itemMsg);
    }
    tolk::Speak(fullMsg, /*interrupt=*/true);
    acclog::Write("Cycle: -> [%s]", fullMsg);
}

// ---- Per-action handlers (shared by both ingestion paths) ----

// Step within the current category and announce the new focused item.
void OnCycleItem(bool prev) {
    acc::cycle::CategoryListing listing;
    acc::cycle::RefreshCurrentListing(listing);
    if (prev) acc::cycle::CyclePrevItem(listing);
    else      acc::cycle::CycleNextItem(listing);
    AnnounceCurrent(listing, /*categoryPrefix=*/nullptr);
}

// Step to the next/prev non-empty category and announce
// "{Category}. {closest item name}, {clock}, {metres}". When all six
// categories are empty, speaks the EmptyAll string without a prefix
// (the prefix would be misleading — no category was actually landed on).
void OnCycleCategory(bool prev) {
    acc::cycle::CategoryListing listing;
    bool found = prev
        ? acc::cycle::CyclePrevCategory(listing)
        : acc::cycle::CycleNextCategory(listing);
    if (!found) {
        const char* msg = acc::strings::Get(acc::strings::Id::EmptyAll);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle: -> [%s] (all empty)", msg);
        return;
    }
    auto& s = acc::cycle::GetState();
    auto bindings = BindingsFor(s.category);
    AnnounceCurrent(listing, acc::strings::Get(bindings.name));
}

// `-` repeats the current focus (same payload as the cycle keys produce
// on the most recent step). Useful when the user wants to re-hear after
// the screen reader was interrupted by something else.
void OnAnnounceFocus() {
    acc::cycle::CategoryListing listing;
    acc::cycle::RefreshCurrentListing(listing);
    AnnounceCurrent(listing, /*categoryPrefix=*/nullptr);
}

// Shift+- — guide the player to the currently-focused Pillar 4 object via
// the cross-cutting acc::guidance::WalkTo wrapper (lay-off 5). Plays the
// per-category 3D cue at the destination as spatial confirmation, then
// speaks the localized "Guiding to {name}" payload.
//
// Empty-state: when no item is focused (user hasn't cycled, or the
// previously-focused object dropped out of scope), speaks the localized
// "No object focused" phrase and skips the WalkTo call.
//
// Cancel-on-second-press: when an autowalk is in flight, the next
// Shift+- press cancels it via `acc::guidance::CancelMovement` (wraps
// `CSWSObject::ClearAllActions @ 0x004ccd80`). RE'd 2026-05-04;
// implementation 2026-05-04. Engine-convention cancel — any directional
// input from the player interrupts auto-walk — also still works.
// Re-pressing Shift+- when NOT in flight dispatches a fresh walk to
// the currently-focused Pillar 4 object (or speaks GuidanceNoFocus if
// nothing is focused).
void OnPathfindFocus() {
    // Toggle-cancel branch — runs before the focus check so the user
    // can cancel with no focus selected (e.g. cycled to a focused
    // object, walked, then cycled past the end → focus dropped, but
    // they want to stop the in-flight walk).
    if (acc::guidance::IsAutowalkInFlight()) {
        bool ok = acc::guidance::CancelMovement();
        if (ok) {
            // Restore manual control immediately rather than waiting
            // for the 3s auto-restore — the user wants the keyboard
            // back NOW.
            acc::engine::SetPlayerInputEnabled(true);
            const char* msg = acc::strings::Get(
                acc::strings::Id::MovementCancelled);
            tolk::Speak(msg, /*interrupt=*/true);
            acclog::Write("Cycle: Shift+- -> [%s] (cancel path)", msg);
            return;
        }
        // Cancel SEH-faulted — fall through to walk so the second
        // press at least does *something*. Local in-flight state is
        // already cleared by CancelMovement's SEH path.
        acclog::Write(
            "Cycle: Shift+- cancel SEH-FAULT, falling through to walk");
    }

    acc::cycle::CategoryListing listing;
    acc::cycle::RefreshCurrentListing(listing);
    auto& s = acc::cycle::GetState();

    if (!s.focusedObj || s.focusedIndex < 0 ||
        s.focusedIndex >= listing.count) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle: Shift+- -> [%s]", msg);
        return;
    }

    const Vector& dest = listing.positions[s.focusedIndex];

    // Per-category 3D cue at the destination — same spatial-confirmation
    // pattern as the cycle keys produce. Reuses the cycle's category-cue
    // mapping rather than introducing a guidance-specific cue (the user
    // already knows the category from the cycle, and Pillar 1 hasn't
    // shipped a guidance-specific cue yet).
    auto bindings = BindingsFor(s.category);
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(bindings.cue), dest);

    char name[128] = "";
    if (!acc::engine::GetObjectName(s.focusedObj, name, sizeof(name)) ||
        name[0] == '\0') {
        // Fall back to the localized category name when the object's
        // CExoLocString chain produces nothing — same convention as
        // AnnounceCurrent so an unnamed door still speaks as "Tür" /
        // "Door" rather than empty.
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(bindings.name));
    }

    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtGuidingTo),
                  name);

    bool ok = acc::guidance::WalkTo(dest);
    if (ok) {
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle: Shift+- -> [%s] obj=%p "
                      "dest=(%.2f,%.2f,%.2f) dist=%.2fm (queue path)",
                      msg, s.focusedObj,
                      dest.x, dest.y, dest.z,
                      listing.distances[s.focusedIndex]);
    } else {
        // WalkTo returns false only when no player creature is loaded
        // (impossible here — TryHandleEvent / PollWin32 already gated on
        // GetPlayerPosition succeeding) or the engine call faulted under
        // SEH (engine teardown). Speak the localized failure phrase so
        // the user can distinguish keypress-eaten from action-failed
        // (per memory `feedback_never_silence_fallback_announcement`).
        char failMsg[192];
        std::snprintf(failMsg, sizeof(failMsg),
                      acc::strings::Get(acc::strings::Id::FmtGuidingFailed),
                      name);
        tolk::Speak(failMsg, /*interrupt=*/true);
        acclog::Write("Cycle: Shift+- -> [%s] WalkTo FAILED obj=%p "
                      "dest=(%.2f,%.2f,%.2f)",
                      failMsg, s.focusedObj, dest.x, dest.y, dest.z);
    }
}

// Alt+- — diagnostic alternate path that bypasses the action queue via
// CSWSCreature::ForceMoveToPoint instead of AddMoveToPointAction. Same
// payload semantics (cue + Tolk announce); only the engine entry point
// differs. Used to discriminate "queue contention" from "input-mode /
// flag-bit" failure modes when WalkTo is silently dropped.
//
// Speech is identical to OnPathfindFocus — the user's audible feedback
// is the same so behaviour comparison is purely about whether the
// character actually moves. The log distinguishes via "(force path)" /
// "(queue path)" tags so post-mortem grep finds each path cleanly.
void OnPathfindFocusForce() {
    acc::cycle::CategoryListing listing;
    acc::cycle::RefreshCurrentListing(listing);
    auto& s = acc::cycle::GetState();

    if (!s.focusedObj || s.focusedIndex < 0 ||
        s.focusedIndex >= listing.count) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle: Alt+- -> [%s]", msg);
        return;
    }

    const Vector& dest = listing.positions[s.focusedIndex];

    auto bindings = BindingsFor(s.category);
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(bindings.cue), dest);

    char name[128] = "";
    if (!acc::engine::GetObjectName(s.focusedObj, name, sizeof(name)) ||
        name[0] == '\0') {
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(bindings.name));
    }

    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtGuidingTo),
                  name);

    bool ok = acc::guidance::ForceWalkTo(dest);
    if (ok) {
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle: Alt+- -> [%s] obj=%p "
                      "dest=(%.2f,%.2f,%.2f) dist=%.2fm (force path)",
                      msg, s.focusedObj,
                      dest.x, dest.y, dest.z,
                      listing.distances[s.focusedIndex]);
    } else {
        char failMsg[192];
        std::snprintf(failMsg, sizeof(failMsg),
                      acc::strings::Get(acc::strings::Id::FmtGuidingFailed),
                      name);
        tolk::Speak(failMsg, /*interrupt=*/true);
        acclog::Write("Cycle: Alt+- -> [%s] ForceWalkTo FAILED obj=%p "
                      "dest=(%.2f,%.2f,%.2f)",
                      failMsg, s.focusedObj, dest.x, dest.y, dest.z);
    }
}

}  // namespace

bool TryHandleEvent(int param_1, int param_2) {
    // Shift state tracking — fires on both press and release, never consumed.
    // Notify peek_description on the release edge so its block cursor resets
    // (so the next Shift+arrow starts at block 0 again).
    if (param_1 == kInputKbLeftShift || param_1 == kInputKbRightShift) {
        bool wasHeld = g_engineShiftHeld;
        g_engineShiftHeld = (param_2 != 0);
        if (wasHeld && !g_engineShiftHeld) {
            acc::peek::OnShiftReleased();
        }
        return false;
    }

    // Press only beyond this point.
    if (param_2 == 0) return false;

    // Filter to the three codes we care about before any further work.
    if (param_1 != kInputKbComma  &&
        param_1 != kInputKbPeriod &&
        param_1 != kInputKbAnnounce) {
        return false;
    }

    // Gate on "is the player actually in-game?". GetPlayerPosition returns
    // false on menus / chargen pre-spawn / area-load mid-flight, in which
    // case we let the key pass through to its normal engine handler.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return false;

    if (param_1 == kInputKbComma) {
        if (g_engineShiftHeld) OnCycleCategory(/*prev=*/true);
        else                   OnCycleItem    (/*prev=*/true);
        return true;
    }
    if (param_1 == kInputKbPeriod) {
        if (g_engineShiftHeld) OnCycleCategory(/*prev=*/false);
        else                   OnCycleItem    (/*prev=*/false);
        return true;
    }
    if (param_1 == kInputKbAnnounce) {
        if (g_engineShiftHeld) OnPathfindFocus();
        else                   OnAnnounceFocus();
        return true;
    }
    return false;
}

void PollWin32() {
    // VK codes for the three cycle keys.
    //   VK_OEM_COMMA  (0xBC) — `,<` key (same VK on QWERTY + QWERTZ)
    //   VK_OEM_PERIOD (0xBE) — `.>` key (same VK on QWERTY + QWERTZ)
    //   announce      — physical key right of `.`. Layout-dependent VK:
    //                   · US QWERTY: `/?` → VK_OEM_2     (0xBF)
    //                   · DE QWERTZ: `-_` → VK_OEM_MINUS (0xBD)
    //                   We listen for either so the same physical row works
    //                   on both layouts. (On US QWERTY VK_OEM_MINUS is the
    //                   key right of `0`, which becomes a secondary "announce"
    //                   binding — harmless.)
    // VK_SHIFT is the OS-level "either shift" virtual key.
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    static bool s_prevComma    = false;
    static bool s_prevPeriod   = false;
    static bool s_prevAnnounce = false;

    bool comma    = down(VK_OEM_COMMA);
    bool period   = down(VK_OEM_PERIOD);
    bool announce = down(VK_OEM_2) || down(VK_OEM_MINUS);
    bool shift    = down(VK_SHIFT);
    // VK_MENU = either Alt key. Used for the Alt+- diagnostic that routes
    // through ForceWalkTo (queue-bypass) instead of WalkTo (queue-enqueue).
    // Note: holding Alt alone activates the Windows menu bar; harmless in
    // a fullscreen game. Alt+key combinations don't trigger system menus.
    bool alt      = down(VK_MENU);

    bool risingComma    = comma    && !s_prevComma;
    bool risingPeriod   = period   && !s_prevPeriod;
    bool risingAnnounce = announce && !s_prevAnnounce;

    s_prevComma    = comma;
    s_prevPeriod   = period;
    s_prevAnnounce = announce;

    if (!risingComma && !risingPeriod && !risingAnnounce) return;

    // Ignore presses while the game window doesn't have foreground focus
    // — otherwise we'd fire when the user types `,/./-` in another app.
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    // Gate on in-world. In menus / chargen / pre-spawn this short-circuits
    // before any cycle effect.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return;

    if (risingComma) {
        if (shift) OnCycleCategory(/*prev=*/true);
        else       OnCycleItem    (/*prev=*/true);
    }
    if (risingPeriod) {
        if (shift) OnCycleCategory(/*prev=*/false);
        else       OnCycleItem    (/*prev=*/false);
    }
    if (risingAnnounce) {
        // Modifier precedence: Alt routes to the diagnostic Force path
        // before Shift's queue path. Alt+Shift+- still goes Force —
        // simpler than introducing a tri-state, and the user would have
        // to deliberately combine to land in that case.
        if (alt)        OnPathfindFocusForce();
        else if (shift) OnPathfindFocus();
        else            OnAnnounceFocus();
    }
}

}  // namespace acc::cycle_input
