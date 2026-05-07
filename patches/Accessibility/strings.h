// User-facing string table.
//
// Layer: i18n/ — sits between the speech path (cycle_input, future
// announce paths) and Tolk. Centralises every string the user hears so
// language swaps + wording changes happen in one place.
//
// Logs stay English regardless of the active language: developer-readable
// log lines (acc::filter::CategoryName etc.) are NOT routed through this
// table. Only spoken strings use Get().
//
// Encoding: source files use 7-bit ASCII for English and Windows-1252
// hex escapes (e.g. "T\xFCr") for German, so the literal bytes already
// match Tolk's ANSI overload (which calls MultiByteToWideChar(CP_ACP)
// — CP_ACP = Windows-1252 on a German Windows install). UTF-8 source
// would also work but only if the build script sets /utf-8 explicitly,
// which create-patch.bat does not.
//
// Format-string IDs use printf-style placeholders. The argument order is
// stable across languages (`%s` first for the name, `%d` for clock, `%d`
// for metres) — German just keeps the same order with different unit
// words and the "auf X Uhr" navigation idiom.

#pragma once

namespace acc::strings {

enum class Id : int {
    // ---- Singular category names — used as the prefix in
    //      category-cycle announcements ("{Category}. {item}, ...").
    CategoryDoor,
    CategoryNpc,
    CategoryContainer,
    CategoryItem,
    CategoryLandmark,
    CategoryTransition,

    // ---- Empty-category messages — full pre-formatted phrases. The
    //      plural form differs per category (German: T\xFCren, Personen,
    //      Beh\xE4lter, Gegenst\xE4nde, Orte, \xDCberg\xE4nge), and even
    //      English has the singular/plural split, so it's cleaner to
    //      write each empty-state independently than to template it.
    EmptyDoors,
    EmptyNpcs,
    EmptyContainers,
    EmptyItems,
    EmptyLandmarks,
    EmptyTransitions,
    EmptyAll,

    // ---- Per-item announce format templates.
    //      `WithClock`   takes (name, clock_int, metres_int).
    //      `NoClock`     takes (name, metres_int) — used when the player
    //                    yaw is degenerate (mid-spawn / area-load).
    //      `CategoryItem` takes (category_str, item_already_formatted).
    //                    Wraps a category prefix around an already-rendered
    //                    item announcement; used by category cycle.
    FmtAnnounceWithClock,
    FmtAnnounceNoClock,
    FmtCategoryItem,

    // ---- Pillar 4 → guidance binding (Shift+-).
    //      `FmtGuidingTo`     takes the resolved focused-object name (`%s`).
    //      `FmtGuidingFailed` takes the same `%s` — spoken when WalkTo
    //                         returns false (engine teardown / SEH fault).
    //                         Per the user-feedback rule
    //                         `feedback_never_silence_fallback_announcement`:
    //                         silent failure leaves the user unable to
    //                         distinguish keypress-eaten vs. action-failed.
    //      `GuidanceNoFocus`  is spoken when Shift+- fires with no item
    //                         focused (e.g. user hasn't cycled yet, or the
    //                         previously-focused object dropped out of
    //                         scope).
    //      `GuidingToPoint`   is spoken when view-mode Enter / Shift+Enter
    //                         dispatches a raw walk-to-cursor (no hover
    //                         target under the virtual cursor). No format
    //                         args — the destination is the cursor's world
    //                         position, which has no name.
    FmtGuidingTo,
    FmtGuidingFailed,
    GuidanceNoFocus,
    GuidingToPoint,

    // ---- Cancel-on-second-press (Shift+- toggle behaviour). Spoken
    //      when an in-flight autowalk is cancelled via
    //      `acc::guidance::CancelMovement`. No format args — fixed
    //      phrase so the user immediately knows the toggle latched.
    MovementCancelled,

    // ---- Lay-off 9b combined autowalk+interact hotkey (Enter).
    //      Per-kind pre-roll spoken when the hotkey fires on a focused
    //      object, before the engine click pipeline runs.
    //      `FmtInteractTalk`  — NPCs (talk to / start dialog).
    //      `FmtInteractOpen`  — doors, containers, placeables (use).
    //      `FmtInteractTake`  — items (pick up).
    //      `FmtInteractFailed` — generic failure pre-roll if the engine
    //                            entry point faults under SEH.
    FmtInteractTalk,
    FmtInteractOpen,
    FmtInteractTake,
    FmtInteractFailed,

    // ---- Engine-picked action pre-roll. Used when the engine action
    //      picker (CClientExoAppInternal::GetDefaultActions) returns a
    //      localised verb for the focused target (e.g. "Sicherheit",
    //      "Angriff", "Bash", "Disable Trap"). Two args:
    //      `%s` action verb (already localised by the engine via
    //          dialog.tlk; we pass it through unmodified), then
    //      `%s` target name (CSWSObject tag / first_name).
    //      Falls back to the per-kind FmtInteract* phrases when the
    //      engine descriptor is empty.
    FmtInteractEngine,

    // ---- Radial-menu opened pre-roll. Used when the engine has no
    //      default action for the focused target (count==0) and we
    //      open the action picker (CSWGuiMainInterface::PopulateMenus)
    //      so the user can pick from the available non-default options
    //      (Security, Bash, Examine, …). One arg: `%s` target name.
    FmtInteractRadial,

    // ---- Player action bar (Aktionsmenü) submenu, armed by Shift+4..7.
    //      `FmtActionBarOpened`      — opener pre-roll. Args: column
    //                                  number 1..6 (`%d`), current variant
    //                                  label (`%s`), variant count (`%d`).
    //                                  Spoken once on Open before the user
    //                                  starts cycling.
    //      `FmtActionBarColumnEmpty` — empty-column refusal. Arg: column
    //                                  number 1..6 (`%d`). Spoken when
    //                                  Shift+N hits a column with no
    //                                  populated variants (no medikit
    //                                  equipped, no Force power memorised
    //                                  in that slot, …).
    //      `ActionBarColumnEmpty`    — generic empty fallback (no column
    //                                  number). Spoken when the format
    //                                  template above is itself empty.
    //      `FmtActionBarFired`       — fire confirmation. Arg: variant
    //                                  label (`%s`). Spoken on Enter inside
    //                                  the submenu.
    //      `ActionBarCancelled`      — Esc-out without firing. Fixed phrase.
    FmtActionBarOpened,
    FmtActionBarColumnEmpty,
    ActionBarColumnEmpty,
    FmtActionBarFired,
    ActionBarCancelled,

    // ---- Container loot panel announces.
    //      `ContainerEmpty`        — count == 0.
    //      `ContainerOneItem`      — count == 1 (German singular has different
    //                                stem: "Gegenstand" vs "Gegenst\xE4nde";
    //                                trying to template that with %d would
    //                                read wrong on the 1-case).
    //      `FmtContainerItems`     — count >= 2; takes (count_int).
    //      `FmtContainerItemAt`    — per-row navigation; takes (row_text_str,
    //                                index_one_based_int, total_int).
    ContainerEmpty,
    ContainerOneItem,
    FmtContainerItems,
    FmtContainerItemAt,

    // ---- Equipment screen (CSWGuiInGameEquip).
    //      Slot button names — used as the speech for each of the 9
    //      paper-doll BTN_INV_* buttons. The .gui file gives them no
    //      strrefs (all 4294967295), so the per-kind fallback path in
    //      ExtractAnnounceableText looks up dialog.tlk strrefs first
    //      (TLK 31375-31383 block, contiguous + locale-stable) and falls
    //      back to these literals if TLK lookup fails. Keeps a German
    //      install reading the engine's exact slot names while a non-
    //      German install still gets a sensible label.
    //
    EquipSlotHead,
    EquipSlotImplant,
    EquipSlotBody,
    EquipSlotArmL,
    EquipSlotArmR,
    EquipSlotWeapL,
    EquipSlotWeapR,
    EquipSlotBelt,
    EquipSlotHands,

    // ---- Pillar 2 transitions (room + area). Both take a single `%s`
    //      with the resolved name. Area names come from CSWSArea.name
    //      (CExoLocString — localized when the area has a name strref) or
    //      fall back to the area tag (modder ID like "tar_m02ac"). Room
    //      names come from CSWSArea.room_names which holds .lyt-room
    //      identifiers like "m02_03e" — NOT localized; the "Room:"/"Raum:"
    //      prefix is what tells the user this is a room name and not an
    //      arbitrary token.
    FmtTransitionArea,
    FmtTransitionRoom,

    // Synthesised room label, used when CSWSArea.room_names[index]
    // resolves to a resref-style identifier (e.g. `m01aa_10`,
    // `stunt_03_main`) — vanilla KOTOR content uses the .lyt-room
    // names verbatim, which read as letter-soup noise through a
    // screen reader. The index is unique-within-area and
    // pronounceable; takes a single `%d`. See
    // transitions::IsResrefStyleRoomName for the heuristic.
    FmtTransitionRoomIndex,

    // Pre-load destination announce, fired by the
    // `CServerExoApp::SetMoveToModuleString` detour just before the
    // engine starts the loading-screen movie. Param is the destination
    // module's resref (e.g. `"endar_spire"`, `"tar_m02ac"`) — modder
    // identifier, not the localized display name. Prefix tells the
    // user this is the "you're about to load into …" announcement, not
    // the post-load "you arrived in …" one.
    FmtTransitionLoading,

    // ---- Door state suffixes appended by `engine_area::GetObjectName` for
    //      doors. Only the *informative* states get a suffix — the default
    //      "closed and unlocked" reads as bare "Tür"/"Door" so common-case
    //      cycle/narrate stays terse. See `BuildDoorSuffix`.
    DoorOpen,
    DoorLocked,

    // ---- Octagonal compass directions for turn announcement
    //      (Pillar 2 sub-feature C). German uses traditional
    //      "Norden / Osten / S\xFCden / Westen" forms with hyphenated
    //      intercardinals ("Nord-Ost"). English uses bare "North" /
    //      "Northeast" so screen readers don't mispronounce hyphens.
    DirNorth,
    DirNortheast,
    DirEast,
    DirSoutheast,
    DirSouth,
    DirSouthwest,
    DirWest,
    DirNorthwest,

    // ---- On-demand exact-heading announce (Pillar 2 sub-feature D —
    //      `announce_degrees`). Single `%d` argument: compass-frame
    //      degrees in [0, 359]. German speaks "47 Grad", English speaks
    //      "47 degrees" — using "Grad"/"degrees" rather than the "°"
    //      glyph so screen readers pronounce it consistently.
    FmtCompassDegrees,

    // ---- Phase 4 lay-off 2 — view-mode Mouse Look probe (Shift+AltGr).
    //      Diagnostic-only; spoken on toggle so the user knows the
    //      keypress landed and can correlate it with whatever the
    //      engine then does (or doesn't) with mouse motion.
    MouseLookOn,
    MouseLookOff,

    // ---- Phase 4 lay-off 3 — view mode toggle (B). The "stop and look
    //      around" mode: character is frozen, camera stays under the
    //      user's control, no triggers / combat fire while active. The
    //      skeleton lay-off captures Mouse Look state + cursor position
    //      on enter; further input mapping (keyboard-driven camera) lands
    //      in lay-off 4 once the camera-behavior probe (Shift+B) tells
    //      us whether we drive the engine's native Free Look or fall
    //      back to forcing Mouse Look ON.
    ViewModeOn,
    ViewModeOff,

    // ---- Save / Load game panel (CSWGuiSaveLoad).
    //      `FmtSaveLoadRow`      — row navigation with location enrichment.
    //                              Args: row_text (`%s`), planet (`%s`),
    //                              area (`%s`), index_one_based (`%d`),
    //                              total (`%d`).
    //      `FmtSaveLoadRowNoLoc` — row navigation when the entry's areaname
    //                              and lastmodule are both empty (e.g. an
    //                              unused slot). Args: row_text (`%s`),
    //                              index_one_based (`%d`), total (`%d`).
    FmtSaveLoadRow,
    FmtSaveLoadRowNoLoc,

    // ---- Level-up hotkey (Shift+L). Speaks an opener cue when the user
    //      triggers `CGuiInGame::ShowLevelUpGUI` so the screen-reader
    //      user knows the panel was requested even before its controls
    //      register with our chain walker. `LevelUpFailed` is spoken on
    //      SEH fault inside the engine call (chain unresolved, or the
    //      call faulted) — mirrors the autowalk "fehlgeschlagen" pattern
    //      so silent failure can't leave the user wondering whether the
    //      key did anything.
    LevelUpOpen,
    LevelUpFailed,

    Count_,
};

enum class Lang : int {
    En,
    De,
};

// Set the active language. Default is German (per user direction at
// lay-off 4 wiring). Thread-safe in the sense that the patch DLL is
// single-threaded around the speech path; no locking needed.
void SetLanguage(Lang l);
Lang GetLanguage();

// Resolve a string ID to its language-specific bytes. Never returns
// nullptr — out-of-range / Count_ ids resolve to "" so callers can
// snprintf without null-checking.
const char* Get(Id id);

// Per-language tables. Defined in strings_en.cpp / strings_de.cpp; the
// dispatcher in strings.cpp picks one based on the active language.
namespace lang_en { const char* Get(Id id); }
namespace lang_de { const char* Get(Id id); }

}  // namespace acc::strings
