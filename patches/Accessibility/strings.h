// User-facing string table.
//
// Layer: i18n/ — sits between the speech path (cycle_input, future
// announce paths) and Prism. Centralises every string the user hears so
// language swaps + wording changes happen in one place.
//
// Logs stay English regardless of the active language: developer-readable
// log lines (acc::filter::CategoryName etc.) are NOT routed through this
// table. Only spoken strings use Get().
//
// Encoding: source files use 7-bit ASCII for English and Windows-1252
// hex escapes (e.g. "T\xFCr") for German, so the literal bytes already
// match Prism's ANSI overload (which calls MultiByteToWideChar(CP_ACP)
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
    // Map-context override for CategoryLandmark — matches the engine's own
    // "Hinweis" / "map note" terminology on the InGameMap up/down buttons.
    // BindingsFor swaps in this string + EmptyMapHints when the cycle ctx is
    // Map, so blind users hear the same word sighted players see in the UI.
    CategoryMapHint,

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
    EmptyMapHints,
    EmptyAll,
    CycleNoTarget,

    // Map-pin activation phrases — user-placed pins fold into the Map hint
    // cycle alongside waypoints, but have no game-object semantics. Shift+-
    // (autowalk) and Enter (interact) speak hints; Ctrl+- beacons to the
    // pin position; Alt+- is unsupported. MapPinNoText is the fallback name
    // when GetMapPinNoteText returns empty (shouldn't happen for user pins —
    // BuildAutoName always assigns one — but defended for safety).
    MapPinNoText,
    MapPinShiftDashHint,
    MapPinAltDashUnsupported,
    MapPinInteractHint,

    // Saved user markers. Shift+N on the map drops a pin at the cursor's
    // world position; the pin folds into the Map hint cycle.
    //
    //   FmtSavedMarkerAutoNumber   — fallback name when no room context
    //                                resolves at the cursor. 1 `%d`
    //                                (per-area sequence). Ex: "Marker 1".
    //   FmtSavedMarkerAutoWithRoom — combined name when a landmark /
    //                                friendly room name covers the
    //                                cursor's room. 1 `%s` (room name)
    //                                + 1 `%d` (sequence). Ex: "Brücke -
    //                                Marke 1".
    //   FmtSavedMarkerPlaced       — confirmation spoken on success.
    //                                1 `%s` (the resolved name).
    //   SavedMarkerFailed          — spoken when CreateMapPin returns
    //                                false. Per feedback_never_silence_-
    //                                fallback_announcement: speak, don't drop.
    FmtSavedMarkerAutoNumber,
    FmtSavedMarkerAutoWithRoom,
    FmtSavedMarkerPlaced,
    SavedMarkerFailed,

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

    // ---- Pillar 3 Mode B beacon (Ctrl+-).
    //      `FmtBeaconStarted`     — opener spoken when StartBeacon arms.
    //                               One `%s` (resolved destination name).
    //      `BeaconCancelled`      — cancel-on-second-press toggle. Fixed.
    //      `FmtBeaconNoPath`      — A* returned empty / no path found.
    //                               One `%s` (destination name).
    //      `BeaconAlreadyAtDest`  — player is already within reach of the
    //                               focused object; no path to describe.
    //      `FmtRouteHeader`       — assembled route description sentence.
    //                               Args: name (`%s`), total metres (`%d`),
    //                               comma-joined segment list (`%s`), and
    //                               the transition-note tail (`%s`).
    //      `FmtRouteSegment`      — single segment payload. Args: metres
    //                               (`%d`), localised compass direction
    //                               word (`%s` — DirNorth.. via strings.Get).
    //      `RouteJoinSeparator`   — joiner between segments (typically
    //                               ", " in both languages; centralised so
    //                               punctuation tweaks happen here).
    //      `RouteOneTransition`   — tail "1 Übergang" / "1 transition".
    //      `RouteNoTransition`    — tail "kein Übergang" / "no transition".
    FmtBeaconStarted,
    BeaconCancelled,
    FmtBeaconNoPath,
    BeaconAlreadyAtDest,
    FmtRouteHeader,
    FmtRouteSegment,
    RouteJoinSeparator,
    RouteOneTransition,
    RouteNoTransition,
    // `FmtBeaconNextSegment` — re-announced after each waypoint-reached
    // event so the user doesn't have to keep the full multi-segment
    // route description in working memory. Args: metres (`%d`),
    // localised compass direction word (`%s`). Distinct phrasing from
    // FmtRouteSegment ("Weiter X Meter Y" vs the raw "X Meter Y")
    // marks it as an in-flight cue, not part of the opening overview.
    FmtBeaconNextSegment,

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
    // Same fire-confirmation slot, but with the queue position the user
    // just landed on. Args: variant label (`%s`), 1-indexed queue
    // position (`%d`). Used in place of FmtActionBarFired whenever the
    // post-fire combat-round queue size is readable.
    FmtFireAtPosition,
    // Cap-hit confirmation. Arg: variant label (`%s`). Spoken in place
    // of FmtFireAtPosition when the engine's combat-round queue was
    // already at the hard 4-entry cap before the press; the engine
    // silently free's the new action node in that case and the press
    // has no effect. Inferred from the count field staying at 4
    // (pre-press depth == post-press depth == 4).
    FmtFireQueueFull,
    ActionBarCancelled,

    // ---- Generic tooltip fallback.
    //      `NoTooltipAvailable` — spoken when the user presses Shift+arrow
    //                             on a control / variant whose engine
    //                             tooltip is empty. Lets the user know
    //                             the gesture was registered without
    //                             leaving them guessing why nothing was
    //                             said.
    NoTooltipAvailable,

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

    //      `FmtItemStackSuffix` — appended to inventory + container row
    //          announcements when CSWSItem.stack_size > 1 (medpacs, stims,
    //          grenades — anything the engine renders with a count overlay
    //          on its icon). One `%d` (stack count). Caller stays silent
    //          on stack_size == 1 so the user doesn't hear "1" after every
    //          weapon name.
    FmtItemStackSuffix,

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

    //      Slot announce composition. The per-kind slot extractor reads
    //      the equipped item handle from the panel's cached slot fields
    //      (CSWGuiInGameEquip @+0x427c..+0x429c) and resolves it via the
    //      engine's universal name accessor. Sighted players see the
    //      item icon inside each slot; we surface the same info by
    //      appending the item name to the slot label.
    //   FmtEquipSlotItem  — 2 `%s` (slot label, item name). Ex:
    //                       "K\xF6rper, Kampfanzug".
    //   FmtEquipSlotEmpty — 1 `%s` (slot label). Ex: "K\xF6rper, leer".
    FmtEquipSlotItem,
    FmtEquipSlotEmpty,

    //      Virtual stat-row chain entries appended at the END of the
    //      Equip panel chain (after the 9 slot buttons + Back/Change*).
    //      Mirror of menus_credits and menus_charsheet — each row is a
    //      text-only chain entry anchored on the inline stat label
    //      inside CSWGuiInGameEquip. Sighted players see these rendered
    //      at the bottom of the screen; the chain entries let the
    //      keyboard user navigate there and hear the composed phrase.
    //   FmtEquipVitality       — 1 `%s` (engine-rendered "120/120").
    //   FmtEquipDefense        — 1 `%s` (engine-rendered AC value).
    //   FmtEquipAttack         — single-weapon attack. 1 `%s` (to-hit).
    //   FmtEquipAttackDual     — dual-wield attack. 2 `%s` (left, right).
    //   FmtEquipDamage         — 1 `%s` (engine-rendered "1d8+2").
    FmtEquipVitality,
    FmtEquipDefense,
    FmtEquipAttack,
    FmtEquipAttackDual,
    FmtEquipDamage,
    FmtEquipDamageDual,

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

    // ---- Stuck-direction probe (Pillar 1 sub-feature). Fired by
    //      audio_footstep_suppress when the player's leader-walk animation
    //      has been running for >=2s with negligible displacement — they
    //      pressed W but a wall + follower-huddle combo boxes them. The
    //      probe tests 8 cardinals against the cached walkmesh + nearby
    //      bodies and speaks the clear directions so the user can pick
    //      one to step toward without sighted spatial awareness.
    //
    //      `StuckFreeDirsPrefix` — leader word. Followed by ": " + a
    //         comma-joined list of DirNorth..DirNorthwest words.
    //      `StuckAllBlocked`     — fixed-phrase fallback used when the
    //         probe finds zero clear directions (truly boxed). No args.
    StuckFreeDirsPrefix,
    StuckAllBlocked,

    // ---- On-demand exact-heading announce (Pillar 2 sub-feature D —
    //      `announce_degrees`). Single `%d` argument: compass-frame
    //      degrees in [0, 359]. German speaks "47 Grad", English speaks
    //      "47 degrees" — using "Grad"/"degrees" rather than the "°"
    //      glyph so screen readers pronounce it consistently.
    FmtCompassDegrees,

    // ---- Phase 6 lay-off 2 — map-state announcement (AltGr while the
    //      InGameMap panel is foreground). Builds a multi-segment line
    //      describing where the player is + how they're oriented in the
    //      *map* frame (which may be rotated 0/90/180/270° from world
    //      space, per CSWSAreaMap::GetMapRotateCCWFromWorldOrientation
    //      @0x00578ed0). The room name uses the same three-tier
    //      resolution the in-world Pillar 2 transition path uses —
    //      Bioware landmark waypoint → friendly room_name → Raum-N
    //      synthetic fallback.
    //
    //      `FmtMapStateOriented`   — full line. Args: room/landmark
    //                                name (`%s`), compass-frame degrees
    //                                in [0,359] (`%d`), sector word
    //                                (`%s` — DirNorth.. via strings.Get).
    //      `FmtMapStateUnknownRoom` — same line but without the room
    //                                portion (player is outside any
    //                                room walk-zone — rare; happens on
    //                                transition strips). Args: degrees
    //                                (`%d`), sector word (`%s`).
    FmtMapStateOriented,
    FmtMapStateUnknownRoom,

    // ---- World-frame AltGr companion to FmtMapState*. Fires when the
    //      InGameMap panel is NOT foreground (player is just standing in
    //      the world). Reads the camera heading as a sector word + exact
    //      degrees and adds the current perceptual cluster label
    //      (wall_topology::LookupAt) so the user can re-orient without
    //      walking. Cluster vs .lyt-room: the map variant uses .lyt rooms
    //      because that matches what the map UI labels; in the world we
    //      want the perceptual region (corridor / junction / Platz) the
    //      player is *in*, which is the wall-topology cluster.
    //
    //      `FmtWorldStateOriented`        — direction word then cluster.
    //                                       Args in this order: sector
    //                                       word (`%s` — DirNorth.. via
    //                                       strings.Get) then cluster
    //                                       label (`%s`). Degrees are
    //                                       deliberately dropped — the
    //                                       user prefers a clean two-
    //                                       segment cue, exact heading
    //                                       belongs on a dedicated probe.
    //      `FmtWorldStateUnknownCluster`  — same line minus the cluster
    //                                       segment, used when LookupAt
    //                                       returns no graph / off-snap /
    //                                       all-blocked. Single arg:
    //                                       sector word (`%s`).
    FmtWorldStateOriented,
    FmtWorldStateUnknownCluster,

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
    LevelUpAlreadyOpen,

    // ---- Chargen portrait selection (CSWGuiPortraitCharGen).
    //      The left/right arrow buttons have no own text (image-only);
    //      the engine maintains the current portrait name in the parent's
    //      portrait_label. ExtractAnnounceableText composes:
    //        FmtPortraitArrow(direction_label, portrait_name)
    //      so the user hears both the cycle direction and the current
    //      portrait name on focus, and the diff-monitor re-speaks the
    //      composed string when the engine cycles to the next portrait.
    // PortraitLabel — value-display prefix used when the cycle pattern
    //                 lands on a single anchor (left_arrow, with right_arrow
    //                 squashed). Reads as "Porträt" / "Portrait" — the
    //                 user hears the noun, then the cycled value.
    // PortraitArrowPrev / PortraitArrowNext — kept for the
    //                 directional-fallback path (single-arrow chains where
    //                 the anchor consolidation isn't possible).
    PortraitLabel,
    PortraitArrowPrev,
    PortraitArrowNext,
    FmtPortraitArrow,
    // FmtPortraitArrowId — final fallback when neither portrait_label nor
    // the resref-derived description resolve. Args: direction (`%s`),
    // portrait_id one-based (`%d`).
    FmtPortraitArrowId,
    // Portrait description tokens — composed from the engine's live
    // portrait resref on CSWCObject (+0xa8 inline CResRef). The chargen
    // resrefs follow `po_p[mf]h[abc]\d` (gender + race code + variant);
    // mapping each character to a localised word lets us build a screen-
    // reader-friendly name without hard-coding the 30 PC portrait rows.
    // FmtPortraitDescription: 2 `%s` (gender, race) + 1 `%d` (variant).
    PortraitGenderFemale,
    PortraitGenderMale,
    PortraitRaceAsian,
    PortraitRaceDark,
    PortraitRaceLight,
    FmtPortraitDescription,

    // ---- Party selection portrait status suffix. Appended (with leading
    //      ", ") to the companion name when speaking a portrait in the
    //      Gruppenauswahl panel. Two states the user can hit on this
    //      panel:
    //        FmtPartyPortraitInTeam    — slot is currently in the active
    //                                    party (partyId >= 0 in the
    //                                    portrait struct). One `%s` (name).
    //        FmtPartyPortraitAvailable — slot is recruited and on the
    //                                    bench (PartyTableIsNPCAvailable
    //                                    returns true, partyId < 0). One
    //                                    `%s` (name).
    FmtPartyPortraitInTeam,
    FmtPartyPortraitAvailable,

    // ---- Disabled-button suffix. Appended (with leading ", ") to the
    //      announced text when a chain-navigable button's is_active field
    //      (CSWGuiControl +0x4C) reads zero — the engine's universal
    //      enabled/disabled flag. Verified against the four LevelUp step
    //      handlers (CSWGuiLevelUpPanel::OnSelectAbilitiesButton etc. at
    //      0x006ee350-0x006ee500), all of which gate `if (is_active != 0)`
    //      and silently drop the click otherwise. The suffix turns the
    //      silent drop into an audible "Attribute, nicht verfügbar" /
    //      "Attribute, unavailable" so the user understands why Enter
    //      does nothing.
    DisabledSuffix,

    // ---- Character sheet sub-screen opener (CSWGuiInGameCharacter).
    //      One announce on first-sight per panel-open cycle, built by
    //      menus_charsheet::MaybeAnnounce. Each Fmt* below is one sentence
    //      fragment in the composed line; fields the engine renders as
    //      empty are skipped at the call site so we don't speak bare
    //      labels with no value.
    //
    //      Class line: takes 1 `%s` (class name as the engine renders it,
    //      e.g. "Soldat" / "Soldier" — already localised via dialog.tlk).
    //      Trailing ". " makes screen readers pause before the next field.
    //      Class name needs no own prefix — coming first, it gives natural
    //      context.
    FmtCharSheetClass,
    // Level: 1 `%s` (numeric value as engine-rendered string).
    FmtCharSheetLevel,
    // Experience: 2 `%s` (current / threshold, both engine-rendered).
    FmtCharSheetXp,
    // Hit points / Force points: 1 `%s` each (typically "999/999").
    FmtCharSheetHp,
    FmtCharSheetFp,
    // Attribute lines (Str/Dex/Con/Int/Wis/Cha): 3 `%s` each — value,
    // separator (", " when a modifier is present, "" otherwise), and
    // pre-formatted modifier ("+2" / "-1") read straight from the engine
    // (so we don't reimplement the +N/-N formatting).
    FmtCharSheetStr,
    FmtCharSheetDex,
    FmtCharSheetCon,
    FmtCharSheetInt,
    FmtCharSheetWis,
    FmtCharSheetCha,
    // Alignment slider: 2 `%u` (cur / max). Vanilla range is 0..100;
    // 50 = neutral, 0 = Dark Side, 100 = Light Side.
    FmtCharSheetAlignment,

    // ---- Chargen Attribute panel (CSWGuiAbilitiesCharGen).
    //      The panel exposes per-row info that isn't in the chain
    //      (Modifier, Cost) and a panel-level budget (Remaining points)
    //      the user needs to budget allocations.
    //
    //      `FmtChargenAttrInfoSuffix` — appended on every chain step
    //          into an ability button, after the existing
    //          "{label}, {value}" announce. Two `%s` (modifier text
    //          rendered by the engine — e.g. "-1", "+2", "0" — and the
    //          per-row cost the next +1 would charge).
    //      Four `FmtChargenAttrValueChange*` variants are picked at
    //      runtime based on whether the +/- press crossed a modifier
    //      breakpoint, a cost breakpoint, both, or neither. The
    //      modifier and cost are NOISY when they don't change (most
    //      presses don't), so we only insert them when their value
    //      differs from the last-announced value on this ability.
    //      The label name is omitted on every path: the user just
    //      navigated to and acted on this row, so the identity is
    //      fresh in context.
    //
    //          Bare           — 2 `%s` (value, remaining).
    //          WithMod        — 3 `%s` (value, modifier, remaining).
    //          WithCost       — 3 `%s` (value, remaining, cost).
    //          WithModAndCost — 4 `%s` (value, modifier, remaining,
    //                                   cost).
    //      Modifier is placed next to the value (it's what changed
    //      the value's effect); cost is at the end (it's the next
    //      forward-looking number).
    FmtChargenAttrInfoSuffix,
    FmtChargenAttrValueChangeBare,
    FmtChargenAttrValueChangeWithMod,
    FmtChargenAttrValueChangeWithCost,
    FmtChargenAttrValueChangeWithModAndCost,

    // ---- Chargen Fähigkeiten panel (CSWGuiSkillsCharGen).
    //      Same shape as the Attribute panel but skills don't have a
    //      D&D modifier — only rank + per-+1 cost (1 for class
    //      skills, 2 for cross-class). The cost is constant per row
    //      throughout the skill's normal range, so the value-change
    //      announce never includes it (the user heard it in the
    //      chain-step suffix).
    //
    //          FmtChargenSkillInfoSuffix — per-row info on chain
    //              step. One `%s` (cost — 1 for class, 2 for
    //              cross-class).
    //          FmtChargenSkillValueChange — value-change announce
    //              after a +/- press. Two `%s` (new value, remaining
    //              budget). No cost / no row label — fresh in
    //              context.
    FmtChargenSkillInfoSuffix,
    FmtChargenSkillValueChange,

    // ---- Chargen Talente panel (CSWGuiFeatsCharGen).
    //      The "second popup" on entry is skillinfo.gui mounted on the
    //      engine's SkillInfoBox slot, populated via ShowGranted with the
    //      feats the chargen class auto-grants at level 1 (different per
    //      class, e.g. Soldat vs. Schurke). Its title label carries a
    //      BioWare dev placeholder ("Items Available to Place in Container
    //      and blah blah blah") that never gets overwritten — substitute
    //      ChargenFeatGrantedTitle. Each row announces with
    //      FmtChargenFeatGrantedRow.
    //
    //          ChargenFeatGrantedTitle — fixed phrase, no args.
    //          FmtChargenFeatGrantedRow — one `%s` (feat name) and two
    //              `%d` (1-based row index, total row count).
    ChargenFeatGrantedTitle,
    FmtChargenFeatGrantedRow,

    // ---- Chargen Talente main panel (CSWGuiFeatsCharGen).
    //      Each chart cell announces its feat name + a status word from
    //      the cell's status byte (0=avail, 1=existing, 2=granted,
    //      3=locked, 4=chosen).
    //
    //          FmtChargenFeatChartCell — "%s, %s" (feat name, status word)
    //          ChargenFeatStatusAvailable / Existing / Granted / Locked /
    //              Chosen — five status words, no args.
    FmtChargenFeatChartCell,
    ChargenFeatStatusAvailable,
    ChargenFeatStatusExisting,
    ChargenFeatStatusGranted,
    ChargenFeatStatusLocked,
    ChargenFeatStatusChosen,

    // ---- Editbox / input field (only the chargen Name screen has one
    //      in vanilla KOTOR, but the speech is shaped to suit any future
    //      editbox).
    //
    //      EditboxRole  — role word ("Eingabefeld" / "edit field"). Spoken
    //                     on focus-enter, separated from the current value
    //                     by ". " in the announce path.
    //      EditboxEmpty — placeholder spoken instead of an empty value
    //                     ("leer" / "empty"). Used both on focus-enter and
    //                     on Up/Down re-read when the field has no text.
    //      EditboxEnd   — caret-at-end-of-string marker ("Ende" / "end").
    //                     Spoken when Right arrow lands the caret past the
    //                     last character (no char to read at the new caret).
    EditboxRole,
    EditboxEmpty,
    EditboxEnd,

    // ---- Combat system, Phase 1A — combat-mode entry/exit announcement.
    //      Spoken when CClientExoApp.combat_mode transitions; debounced via
    //      the stability pattern in camera_announce.cpp.
    CombatBegins,
    CombatEnds,

    // ---- Bare-H / leader-status fallback when no player creature is
    //      resolved (chargen→world transient, minigame slots).
    PcStatNoCharacter,

    // ---- Combat system, Phase 2B — opponent cycle-announcement
    //      enrichment. Composed by combat_query::BuildTargetCombatBrief
    //      and spoken via passive_narrate when ShowObject fires
    //      (Q/E hostile cycle + mouse-hover passive selection).
    //      Composition order mirrors what the sighted player reads from
    //      the target reticle: name, condition, distance, status
    //      effects, main-hand weapon, off-hand weapon.
    //      Args `FmtTargetCombatBrief`: target_name (`%s`).
    //      `FactionHostile` / `FactionFriendly` / `FactionNeutral` —
    //      faction-relation words (still used by the Shift+H examine
    //      row, not by the Q/E brief).
    //      `TargetIsDead` — appended when the creature is dead.
    FmtTargetCombatBrief,
    FactionHostile,
    FactionFriendly,
    FactionNeutral,
    TargetIsDead,

    // Brief enrichment clauses — appended (with leading space) to the
    // FmtTargetCombatBrief base line. Designed for composition so we
    // can add/skip individual fields without permuting the base format.
    //   FmtBriefCondition      — args: damage-level word (`%s`). Skipped
    //                            when the target is at full HP so common
    //                            healthy-target transitions stay terse.
    //   FmtBriefDistanceMeters — args: meters (`%d`). Q/E + Shift+H.
    //   FmtBriefEffects        — args: joined unique effect names (`%s`).
    //                            Q/E only; Shift+H lists each effect
    //                            individually via the examine listbox.
    //   FmtBriefWielding       — args: main-hand item display name (`%s`).
    //   FmtBriefOffHand        — args: off-hand item display name (`%s`).
    //                            Dual-wield / shield / off-hand pistol.
    //   FmtBriefEffectsCount   — args: count (`%d`). Shift+H only.
    //   FmtBriefFeatsCount     — args: count (`%d`). Shift+H only.
    FmtBriefCondition,
    FmtBriefDistanceMeters,
    FmtBriefEffects,
    FmtBriefWielding,
    FmtBriefOffHand,
    FmtBriefEffectsCount,
    FmtBriefFeatsCount,

    // ---- Bare-H self status. HP opener — the rest of the line reuses
    //      FmtBriefEffects / FmtBriefWielding / FmtBriefOffHand so the
    //      wording stays consistent with the Q/E target brief.
    //
    //   FmtSelfStatusHp   — current-only fallback. 1 `%d` (cur).
    //                       Used when the max accessor returns 0 (e.g.
    //                       driving slots, minigame creatures).
    //   FmtSelfStatusHpOf — full "%d of %d" form. Used in normal play.
    FmtSelfStatusHp,
    FmtSelfStatusHpOf,

    // ---- Combat system, Phase 2C — examine hotkey (Shift+H). Spoken on
    //      open / close / no-target failure.
    ExamineOpened,
    ExamineNoTarget,
    ExamineFailed,

    // ---- Examine view — navigable list opened by Shift+H. The view is
    //      a synthetic in-DLL listbox (no engine panel — KOTOR 1 doesn't
    //      have a real creature-examine panel). Up/Down step rows, Esc
    //      closes. See examine_view.cpp.
    //   FmtExamineOpened     — opener cue. Args: target name (`%s`),
    //                          total rows (`%d`).
    //   FmtExamineRowOf      — per-row announce. Args: row text (`%s`),
    //                          1-based index (`%d`), total (`%d`).
    //   ExamineViewClosed    — Esc cue.
    //   FmtExamineRowName    — row label "Name: %s".
    //   FmtExamineRowFaction — row label "Fraktion: %s".
    //   FmtExamineRowHp      — row label "Lebenspunkte: %d".
    //   FmtExamineRowDistance— row label "Entfernung: %d Meter".
    //   FmtExamineRowWeapon  — row label "Hauptwaffe: %s".
    //   ExamineRowWeaponNone — "Hauptwaffe: keine" (unarmed).
    //   FmtExamineRowEffect  — row label "Effekt: %s".
    //   FmtExamineRowFeat    — row label "Talent: %s".
    //   FmtExamineRowEffectUnknown — "Effekt #%d" fallback for unmapped types.
    //   FmtExamineRowFeatUnknown   — "Talent #%d" fallback when feat name
    //                                resolution fails.
    //   ExamineRowNoEffects  — "Keine aktiven Effekte".
    //   ExamineRowNoFeats    — "Keine Talente".
    FmtExamineOpened,
    FmtExamineRowOf,
    ExamineViewClosed,
    FmtExamineRowName,
    FmtExamineRowFaction,
    FmtExamineRowHp,
    FmtExamineRowDistance,
    FmtExamineRowWeapon,
    ExamineRowWeaponNone,
    FmtExamineRowEffect,
    FmtExamineRowFeat,
    FmtExamineRowEffectUnknown,
    FmtExamineRowFeatUnknown,
    ExamineRowNoEffects,
    ExamineRowNoFeats,

    // ---- Examine view, easy-wins extension. Direct field reads + small
    //      engine accessors. All optional rows — present only when the
    //      data is available / meaningful (so a healthy creature doesn't
    //      get a "no status effects" + "not invisible" + "not blind" row).
    //   FmtExamineRowHpFull   — replaces FmtExamineRowHp when HP-max is
    //                           resolved. Args: hp_cur, hp_max.
    //   FmtExamineRowLevel    — args: total class level.
    //   FmtExamineRowCondition — args: localized damage-level word.
    //   DamageLevel0..5       — words for the 6 GetDamageLevel buckets.
    //   FmtExamineRowOffHand   — off-hand item.
    //   FmtExamineRowHead      — head slot.
    //   FmtExamineRowTorso     — torso (visible armor) slot.
    //   FmtExamineRowHands     — hands slot.
    //   ExamineRowStatusInvisible — present only when GetInvisible != 0.
    //   ExamineRowStatusBlind     — present only when GetBlind != 0.
    FmtExamineRowHpFull,
    FmtExamineRowLevel,
    FmtExamineRowCondition,
    DamageLevel0Healthy,
    DamageLevel1Light,
    DamageLevel2Wounded,
    DamageLevel3Badly,
    DamageLevel4Dying,
    DamageLevel5Dead,
    FmtExamineRowOffHand,
    FmtExamineRowHead,
    FmtExamineRowTorso,
    FmtExamineRowHands,
    ExamineRowStatusInvisible,
    ExamineRowStatusBlind,

    // ---- Combat system, Phase 3A — action queue submenu.
    //      `FmtQueueOpen` — opener pre-roll. Args: total queue length
    //                       across all party members (`%d`).
    //      `QueueEmpty` — fixed; spoken on Open with no queued actions
    //                     across any party member.
    //      `FmtQueueRow` — per-row announce on Up/Down. Args: character
    //                      name (`%s`), action verb (`%s`), target name
    //                      (`%s`), 1-based index (`%d`), total (`%d`).
    //                      The leading character name distinguishes whose
    //                      queue this row belongs to — the submenu is
    //                      party-wide (walks all CSWPartyTable members'
    //                      combat_round.actions).
    //      `FmtQueueRemoved` — confirmation after Enter remove. Args: verb.
    //      `QueueCleared` — confirmation after Shift+Enter wipe.
    //      `QueueClosed` — Esc-out without changes.
    //      `QueueRemoveFailed` — spoken if remove primitive fails.
    //      Action verbs are mapped from the `action_type` byte enum
    //      (Ghidra-confirmed 2026-05-14 via GetActionIcon decompile):
    //      1=Attack, 6=Equip, 7=Unequip, 9=Cast Force Power, 10=Use Item,
    //      11=Use Feat. Other / unknown bytes render as QueueVerbUnknown.
    FmtQueueOpen,
    QueueEmpty,
    FmtQueueRow,
    FmtQueueRemoved,
    QueueCleared,
    QueueClosed,
    QueueRemoveFailed,
    QueueVerbAttack,
    QueueVerbCastForce,
    QueueVerbItemCast,
    QueueVerbEquip,
    QueueVerbUnequip,
    QueueVerbMove,
    QueueVerbHeal,
    QueueVerbUseTalent,
    QueueVerbCutscene,
    QueueVerbUnknown,

    // ---- Combat system, Phase 4A — per-attack resolved callout.
    //      `FmtAttackHit` — args: attacker (`%s`), target (`%s`), damage
    //                       (`%d`), target_hp_cur (`%d`), target_hp_max
    //                       (`%d`).
    //      `FmtAttackMiss` — args: attacker, target.
    //      `FmtAttackCrit` — args: attacker, target, damage, hp_cur, hp_max.
    //      `FmtAttackDeflected` — args: attacker, target.
    //      Used by combat::Phase4Tick when an attack record's attack_result
    //      transitions from pending (0) to resolved.
    FmtAttackHit,
    FmtAttackMiss,
    FmtAttackCrit,
    FmtAttackDeflected,

    // ---- Combat system, Phase 4B — saving-throw callout.
    //      `FmtSavingThrowSucceeded` / `FmtSavingThrowFailed` — args:
    //      target (`%s`), saveType (`%s`), roll (`%d`), dc (`%d`).
    //      `SaveTypeFort` / `SaveTypeReflex` / `SaveTypeWill` — type words.
    FmtSavingThrowSucceeded,
    FmtSavingThrowFailed,
    SaveTypeFort,
    SaveTypeReflex,
    SaveTypeWill,

    // ---- Dialog screen, Phase 1D — live conversation replies count cue.
    //      `FmtDialogReplies` — spoken on entry to a node with replies.
    //      Arg: count (`%d`).
    //      `DialogReplyUnavailable` — suffix appended via enrichRow when a
    //      reply is gated (active=0).
    FmtDialogReplies,
    DialogReplyUnavailable,
    // Per-row reply announce when the entry is greyed (active=0). Args:
    // row_text (`%s`), unavailable_word (`%s`), index (`%d`), total
    // (`%d`). Separate from FmtContainerItemAt so the localised "von" /
    // "of" stays in the table.
    FmtDialogReplyUnavailableRow,

    // ---- Messages-panel review titles. Spoken when the InGameMessages
    //      panel becomes foreground (titleOverride). Two channels — the
    //      combat log and the dialog history.
    MessagesTitleCombatLog,
    MessagesTitleDialogLog,

    // ---- In-game map UI (CSWGuiInGameMap, Phase 5 lay-off 6).
    //      Two image-only buttons sit on either side of the map render
    //      (struct fields up_button @+0xab0, down_button @+0xc74). Their
    //      OnUpArrowPressed / OnDownArrowPressed handlers dispatch
    //      HandleInputEvent(0x31/0x32) which route to
    //      CSWGuiMapHider::GetPrevMapNote / GetNextMapNote — i.e. they
    //      cycle the engine's filtered list of explored map-note
    //      waypoints (already spoiler-correct: GetPrev/Next skip nodes
    //      where IsWorldPointExplored is false). The per-kind label
    //      fallback in menus_extract resolves them by panel-base offset.
    MapPrevNote,
    MapNextNote,

    // Spoken by the virtual map cursor when it hover-pauses on a cell
    // the player hasn't yet revealed (CSWSAreaMap::IsWorldPointExplored
    // returns false). Never speak the room/landmark name underneath in
    // that case — fog-of-war must stay spoiler-correct.
    MapCursorUnexplored,

    // Fallback when the cursor sits on an explicit map-note waypoint
    // (HasMapNote=1, MapNoteEnabled=1) whose localised text and TLK
    // strref are both empty. Earlier behaviour kept silent which hid
    // the marker entirely from the user — generic "Point of Interest"
    // is the honest default per feedback_never_silence_fallback_announcement.
    MapCursorWaypointPOI,

    // Terrain shape vocabulary spoken when the cursor sits over an
    // explored, walkable cell with no explicit waypoint/landmark/named-
    // room match. The 4-direction wall probe in map_ui_cursor classifies
    // local walkmesh extents and resolves to one of these.
    MapCursorOpenArea,
    MapCursorJunction,        // bare "Junction" / "Kreuzung" — fallback only
    MapCursorOffPath,
    FmtMapCursorCorridor,     // "%s, %.0f Meter" / "%s, %.0f meters" — direction-axis + length, "Korridor"/"Corridor" noun dropped 2026-05-22 (terser ambient cue)
    FmtMapCursorDeadEnd,      // "Dead end opening %s"
    FmtMapCursorJunctionDirs, // "Junction, openings %s" — comma-separated direction list
    // Path-3 nav-graph topology vocabulary. Corridor labels are
    // norded-out (always point toward the northern half of the world
    // map) so the player never hears "south-corridor"; the axis is a
    // line, not a vector, and showing both ends would double the
    // vocabulary for zero information. Transitions speak as a doorway
    // ("Türschwelle") and optionally compose with the friendly room
    // name in transitions.cpp.
    FmtMapCursorCorridorDir,  // "%s" — direction word alone, "Korridor"/"Corridor" noun dropped 2026-05-22
    // Door announce — replaces the corridor/dead-end/junction-octant
    // direction word when a CSWSDoor in the area sits on the relevant
    // nav-graph edge. Single `%s` = direction word ("Nord", "Nord-Süd",
    // "Süd-West", …). Transition form adds the cross-area destination
    // ("Brücke", "Manaan-Dock") as the second `%s`.
    // Door noun fallback — used as the first %s in the door format
    // strings when CSWSDoor.loc_name @+0x39c is empty / unreadable.
    // Authored door names ("Lift", "Sicherheitstür") substitute for
    // this generic word; vanilla doors with no authored name fall back
    // to it so the output stays identical to the pre-loc-name behaviour.
    MapCursorDoorNoun,           // "Tür" / "Door"
    FmtMapCursorDoor,            // "%s %s"           — noun + direction
    FmtMapCursorDoorTransition,  // "%s %s nach %s"   — noun + direction + dest
    // Landmark variant — used when a CSWCWaypoint with a non-empty map
    // note sits on the door (e.g. "Zur Oberstadt"). Preferred over
    // FmtMapCursorDoorTransition because the landmark name is the
    // content-author's canonical phrasing and reads cleaner than the raw
    // area-transition string (which already starts with "Zur …" / "Zu …"
    // and would yield awkward "… nach Zur Oberstadt"). Comma separator
    // avoids the prep-clash.
    FmtMapCursorDoorLandmark,    // "%s %s, %s"       — noun + direction + landmark
    MapCursorTransitionDoor,  // "Türschwelle" / "Doorway"
    // Junction-edge annotation: when a junction's direction leads
    // directly to a degree-1 dead-end neighbour, the direction word
    // is wrapped to flag the dead-end so the user doesn't waste a
    // walk attempting it. Single `%s` (the direction word).
    FmtMapCursorJunctionDeadEndExit, // "Sackgasse %s" / "dead end %s" — prefix-style, matches "Tür %s" so junction list reads NOUN-then-direction
    // Merged-junction announce. Used in place of FmtMapCursorJunctionDirs
    // when a multi-node cluster has 3+ external exits — semantically the
    // player is at a wider hub area, not a single point junction.
    // wall_topology's announce path delays this label by ~1s so the
    // player has time to walk further into the cluster before the
    // direction list (computed from the cluster centroid) fires —
    // gives more accurate centroid-relative direction perception.
    FmtMapCursorPlazaDirs, // "Platz, %s" / "Place, %s"
    AxisNorthSouth,
    AxisEastWest,

    // ---- Store / trading panel (CSWGuiStore).
    //      The store has two modes (Buy / Sell) toggled by the
    //      examine_button; mode is detected from
    //      shopitems_listbox.bit_flags bit 1 (language-agnostic — see
    //      engine_offsets.h). The chain-step suffix appends price + stock
    //      to each item announce:
    //
    //          FmtStorePriceBuyFinite    — buy mode, finite shop stock.
    //              Two `%d` (price, stock count).
    //          FmtStorePriceBuyUnlimited — buy mode, infinite shop stock.
    //              One `%d` (price).
    //          FmtStorePriceSell         — sell mode (your inventory).
    //              Two `%d` (merchant's offer, count you own).
    //          StoreModeBuy / StoreModeSell — spoken when the mode bit
    //              flips. No args.
    FmtStorePriceBuyFinite,
    FmtStorePriceBuyUnlimited,
    FmtStorePriceSell,
    StoreModeBuy,
    StoreModeSell,
    // ---- Per-trade speech, fired after Enter on a Store row.
    //      `StoreSold` / `StoreBought`     — fired when the active
    //          listbox shrinks/grows within ~2 ticks of DispatchTradeAction
    //          (the trade actually committed).
    //      `StoreCannotSell` / `StoreCannotBuy` — fired when the engine's
    //          handler returned without changing the listbox (item not
    //          sellable / not buyable — plot item, no funds, equipped, etc.).
    StoreSold,
    StoreBought,
    StoreCannotSell,
    StoreCannotBuy,
    // ---- Per-trade speech with price.
    //      `FmtStoreSoldFor`   — successful sell. One `%d` (credits received).
    //      `FmtStoreBoughtFor` — successful buy.  One `%d` (credits paid).
    //      `FmtStoreNotEnoughCredits` — buy refused, player can't afford.
    //          Two `%d` (price required, credits the player has).
    FmtStoreSoldFor,
    FmtStoreBoughtFor,
    FmtStoreNotEnoughCredits,

    // ---- Virtual credits row (Inventory + Store).
    //      Surfaced via menus_credits as a text-only chain entry on
    //      CSWGuiInGameInventory.credits_value_label (@+0x424) and
    //      CSWGuiStore.credits_value_label (@+0x1200). Sighted players see
    //      the label rendered top-right; this entry lets the screen-reader
    //      user navigate to it and hear "Credits: 1247". One `%s` (the
    //      label's gui_string, engine-rendered).
    FmtCredits,

    // ---- Workbench (upgrade.gui / upgradeitems.gui / upgradesel.gui).
    //      Three heap-allocated panels opened by the "Werkbank benutzen"
    //      placeable-conversation reply: category-select → item-pick →
    //      slot detail. The 7 BTN_UPGRADE3X/4X slot buttons on the slot
    //      detail panel have no inline text (their on-screen contents
    //      are the installed mod's icon + name, set programmatically),
    //      so we synthesise speakable labels by .gui ID.
    //
    //      Slot label format:
    //        FmtWorkbenchSlotWeapon   — IDs 12..14 → 1..3. Used for melee /
    //                                   ranged / armor weapons.
    //                                   One `%d` (slot number).
    //        FmtWorkbenchSlotSaberCrystal — IDs 15..18 → 1..4. Used for
    //                                       lightsaber crystal slots.
    //                                       One `%d` (slot number).
    //
    //      Empty-state lines fire when the user lands on an LB_ITEMS that
    //      has no rows:
    //        WorkbenchItemsEmpty    — upgradeitems.gui: no upgradable
    //                                 weapon in the chosen category.
    //        WorkbenchUpgradesEmpty — upgrade.gui: no compatible upgrade
    //                                 mod in the player's inventory.
    WorkbenchSlotWeapon1,
    WorkbenchSlotWeapon2,
    WorkbenchSlotWeapon3,
    WorkbenchSlotSaberCrystal1,
    WorkbenchSlotSaberCrystal2,
    WorkbenchSlotSaberCrystal3,
    WorkbenchSlotSaberCrystal4,
    WorkbenchItemsEmpty,
    WorkbenchUpgradesEmpty,

    // ---- Workbench slot-click outcome announces (non-saber install path).
    //      OnSlotSelected directly installs or removes the upgrade
    //      bound to the slot (set per upgrades_2da row at panel init).
    //      The engine gives no audible feedback for either outcome —
    //      we speak the result by comparing panel.field35_0x2f74[slot]
    //      before vs after the dispatch.
    WorkbenchSlotInstalled,    // 0 → item: install succeeded
    WorkbenchSlotRemoved,      // item → 0: existing upgrade removed
    WorkbenchSlotNoMatch,      // 0 → 0: no inventory item with the slot's expected tag

    // ---- Sound options panel (optionssound.gui) label fix-up.
    //      The stock German .gui labels the 4th slider (movie/video volume)
    //      as "Musik-Lautst\xE4rke" — a duplicate of the 1st slider — and
    //      the engine renders that duplicate verbatim. We don't ship a
    //      .gui override (per project policy), so we substitute the
    //      category label in the screen-reader announcement only.
    //      Detection: slider control id == 8 on a panel whose slider IDs
    //      match the Sound options fingerprint {1, 4, 7, 8} (those IDs
    //      come from the .gui file and are stable across locales).
    SoundOptionsMovieVolume,

    // ---- Swoop racing minigame (tar_m03mg, manm26mg, tat_m17mg).
    //      Continuous spatial cues handle proximity (obstacle loop
    //      + nearest-accelpad loop, see swoop_race.cpp); the spoken
    //      strings here are intentionally terse — full keymap lives
    //      in the manual, the spoken intro must not bleed into the
    //      start countdown.
    //
    //      SwoopRaceStarted   — entry opener, no args. Spoken once when
    //                           CSWCArea.mini_game transitions null→non-null.
    //                           Concatenated with SwoopRaceControls into
    //                           one urgent utterance to survive the
    //                           interrupt-on-speak pattern.
    //      SwoopRaceControls  — short cheat sheet, no args.
    //      SwoopRaceEnded     — exit cue, no args. Spoken on the inverse
    //                           transition.
    //      SwoopRaceObstacleNear — legacy slot from the first pass;
    //                              continuous loop cues replaced the
    //                              per-distance speech, no current
    //                              caller. Kept for future per-strike
    //                              callouts if we wire those up.
    SwoopRaceStarted,
    SwoopRaceControls,
    SwoopRaceEnded,
    SwoopRaceObstacleNear,
    // Gear-shift cue spoken urgently on each shift event so the player
    // hears "Gang 2 / 3 / 4 / 5" through NVDA's typed-character cancel.
    // One %d (1..5). Reset to 1 on race entry and on suspected reset
    // events (collision speed drop). The exact gear field inside
    // CSWMiniPlayer is unconfirmed at this lay-off — see swoop_race.cpp
    // DiagnosticDump for the candidate-pointer-bytes capture.
    FmtSwoopRaceGear,

    // ---- Mod-settings virtual submenu (menus_modsettings).
    //      Root label: text spoken when the user lands on the virtual
    //      "Mod settings" chain entry inside the Optionen panels.
    //      The three option-name strings are the toggle leaves; each is
    //      paired with FmtModSettingOption to compose "Name: state".
    //      Open / Close lines are status announcements (submenu enter /
    //      exit). FmtModSettingOption takes two %s — option name then
    //      state (ModSettingStateOn / Off).
    ModSettingsRootLabel,
    ModSettingsOpened,
    ModSettingsClosed,
    ModSettingExtendedCycling,
    ModSettingRoomShapes,
    ModSettingWallSounds,
    ModSettingStateOn,
    ModSettingStateOff,
    FmtModSettingOption,

    // ---- Mod-settings → Audio glossary submenu (menus_modsettings).
    //      Nested submenu opened by Enter on the "Audio-Glossar" row.
    //      Each row pairs a NavCue with a localised label; Enter on a
    //      row stamps a 1 s delay then plays the cue once at 2D unity.
    //      Door / Npc / Container / Item / Landmark / Transition reuse
    //      the cycle Category* strings — same vocabulary the user
    //      already associates with cycle announcements. Wall / Hazard /
    //      Collision / Beacon* are glossary-specific because the
    //      announce paths name them differently or not at all.
    ModSettingAudioGlossary,
    ModSettingsAudioGlossaryOpened,
    GlossaryEntryDoorOpen,
    GlossaryEntryDoorClosedMetal,
    GlossaryEntryDoorClosedWood,
    GlossaryEntryDoorClosedStone,
    GlossaryEntryWall,
    GlossaryEntryHazard,
    GlossaryEntryCollision,
    GlossaryEntryBeaconActive,
    GlossaryEntryBeaconWaypoint,
    GlossaryEntryBeaconDestination,
    GlossaryEntrySwoopAccelTick,
    GlossaryEntrySwoopAccelpadBoost,
    GlossaryEntrySwoopObstacleWarn,
    GlossaryEntrySwoopWallImpact,

    // ---- In-game auto-updater (UpdateChecker).
    //      Background version check fires on first hook (CSWRules construction);
    //      announcement lands once on the next tick after the check completes.
    //      F5 from the main menu (gated on GetPlayerPosition == false) triggers
    //      installer download + game exit; the calling batch elevates the
    //      installer via the .exe's app.manifest and relaunches the game.
    //
    //      FmtUpdateAvailable    — one `%s` (remote version string from
    //                              GitHub release tag, e.g. "0.3.1").
    //      UpdateDownloading     — F5 acknowledgement spoken once when the
    //                              download task starts.
    //      UpdateDownloaded      — download-complete cue. The mod calls
    //                              ExitProcess shortly after, so the user
    //                              hears this just before the game closes.
    //      UpdateFailed          — download error.
    //      FmtUpdateNotAvailable — F5 when no update is pending. One `%s`
    //                              (current installed version).
    //      UpdateNotInMenu       — F5 refused because the player is in
    //                              active gameplay (GetPlayerPosition
    //                              succeeded).
    FmtUpdateAvailable,
    UpdateDownloading,
    UpdateDownloaded,
    UpdateFailed,
    FmtUpdateNotAvailable,
    UpdateNotInMenu,

    Count_,
};

enum class Lang : int {
    En,
    De,
    Fr,
    It,
    Es,
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

// Per-language tables. Defined in strings_en.cpp / strings_de.cpp /
// strings_fr.cpp; the dispatcher in strings.cpp picks one based on the
// active language. lang_fr currently aliases lang_en for the Id::*
// speech path (full FR translation pass is deferred); combat speech is
// fully French via combat_strings.cpp::kFr.
namespace lang_en { const char* Get(Id id); }
namespace lang_de { const char* Get(Id id); }
namespace lang_fr { const char* Get(Id id); }
namespace lang_it { const char* Get(Id id); }
namespace lang_es { const char* Get(Id id); }

}  // namespace acc::strings
