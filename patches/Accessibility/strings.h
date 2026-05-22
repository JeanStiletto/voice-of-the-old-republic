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
    CategoryMapPin,

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
    EmptyMapPins,
    EmptyAll,

    // Map-pin specific phrases — Shift+- (autowalk) is not supported for
    // map pins because there's no game-object to USE; the user is
    // redirected to Ctrl+- which beacons to the pin's world position.
    // Generic "Quest marker" fallback when the pin's note text is
    // empty (server-side pins always carry text; quest-script pins
    // sometimes start with strref=0 + empty inline).
    MapPinNoText,
    MapPinShiftDashHint,
    MapPinAltDashUnsupported,
    MapPinInteractHint,

    // Phase 6 lay-off 3 — saved user markers. Shift+Q on the map drops
    // a pin at the cursor's world position; the pin enters the existing
    // MapPin cycle category (lay-off 1b) immediately.
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
    //                                false (engine alloc fault or area
    //                                resolution failure). Per
    //                                feedback_never_silence_fallback_
    //                                announcement: speak, don't drop.
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

    // ---- Character sheet (CSWGuiInGameCharacter) — party-member carousel
    //      buttons. The bottom-row icon arrows (btn_charleft / btn_charright
    //      in swkotor.exe.h:9840-9841) cycle the displayed character among
    //      the party via OnSwitchLeft/Right (engine handlers @0x006af450 /
    //      @0x006af6d0). Image-only with no inline text — surfaced through
    //      the per-kind name fallback in menus_extract.
    CharSwitchPrev,
    CharSwitchNext,

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
    //      the stability pattern in turn_announce.cpp.
    CombatBegins,
    CombatEnds,

    // ---- Combat system, Phase 2A — selected-PC full stat block hotkey.
    //      Composed line; each fragment is one Fmt* below. Same pattern
    //      MaybeAnnounce in menus_charsheet.cpp uses.
    //      `FmtPcStatHpFp` — 4 `%d` (hpCur, hpMax, fpCur, fpMax).
    //      `FmtPcStatAc` — 1 `%d` (ac).
    //      `FmtPcStatAttrs` — 6 `%d` (str, dex, con, int, wis, cha).
    //      `FmtPcStatSaves` — 3 `%d` (fort, refl, will).
    //      `FmtPcStatAlignment` — 1 `%d` (alignment 0..100).
    //      `FmtPcStatEffectsHeader` — 1 `%d` (active effect count).
    //      `PcStatHeader` — fixed; spoken first ("Status").
    //      `PcStatNoCharacter` — fallback when no player creature resolved.
    PcStatHeader,
    FmtPcStatHpFp,
    FmtPcStatAc,
    FmtPcStatAttrs,
    FmtPcStatSaves,
    FmtPcStatAlignment,
    FmtPcStatEffectsHeader,
    PcStatNoCharacter,

    // ---- Combat system, Phase 2B — opponent cycle-announcement
    //      enrichment. Appended to the existing passive_narrate target
    //      announce when the target is a creature (hostile or neutral).
    //      Args `FmtTargetCombatBrief`: target_name (`%s`), HP_cur (`%d`),
    //      HP_max (`%d`), AC (`%d`), faction word (`%s`).
    //      `FactionHostile` / `FactionFriendly` / `FactionNeutral` —
    //      faction-relation words.
    //      `TargetIsDead` — appended when the creature is dead.
    FmtTargetCombatBrief,
    FactionHostile,
    FactionFriendly,
    FactionNeutral,
    TargetIsDead,

    // ---- Combat system, Phase 2C — examine hotkey (Shift+H). Spoken on
    //      open / close / no-target failure.
    ExamineOpened,
    ExamineNoTarget,
    ExamineFailed,

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
    FmtMapCursorCorridor,     // "Corridor along %s axis, about %.0f m wide"
    FmtMapCursorDeadEnd,      // "Dead end opening %s"
    FmtMapCursorJunctionDirs, // "Junction, openings %s" — comma-separated direction list
    // Path-3 nav-graph topology vocabulary. Corridor labels are
    // norded-out (always point toward the northern half of the world
    // map) so the player never hears "south-corridor"; the axis is a
    // line, not a vector, and showing both ends would double the
    // vocabulary for zero information. Transitions speak as a doorway
    // ("Türschwelle") and optionally compose with the friendly room
    // name in transitions.cpp.
    FmtMapCursorCorridorDir,  // "Korridor %s" / "Corridor %s"
    // Door announce — replaces the corridor/dead-end/junction-octant
    // direction word when a CSWSDoor in the area sits on the relevant
    // nav-graph edge. Single `%s` = direction word ("Nord", "Nord-Süd",
    // "Süd-West", …). Transition form adds the cross-area destination
    // ("Brücke", "Manaan-Dock") as the second `%s`.
    FmtMapCursorDoor,            // "Tür %s" / "Door %s"
    FmtMapCursorDoorTransition,  // "Tür %s nach %s" / "Door %s to %s"
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

    // ---- Camera orient hotkey (N). When a beacon is armed we face the
    //      camera at the beacon's next waypoint and prefix-speak this so
    //      the user can distinguish a beacon-orient from a plain cardinal
    //      cycle. One `%s`: the resolved compass-sector word
    //      (DirNorth..DirNorthwest) for the new camera facing. The
    //      cardinal-cycle path doesn't speak — camera_announce's sector
    //      cross fires naturally on the rotation.
    FmtCameraOrientBeacon,

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
