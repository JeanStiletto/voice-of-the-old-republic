# Contributing Translations

Thanks for helping make KOTOR 1 playable by ear in more languages. This guide
covers how the mod's spoken strings are organized and how to add or improve a
translation. You do **not** need to build the mod or read any game internals to
translate — you only edit string tables.

If you can build the project, great; if you can't, you can still submit a
translated file and a maintainer will compile and test it.

## What is and isn't translated here

- **Translated:** everything the mod *speaks* — object categories, navigation
  cues, menu labels the mod adds, help text, key names, minigame prompts.
- **Not translated here:** the game's own dialogue, item names, and menus. Those
  come from BioWare's `dialog.tlk` and are read out in whatever language the
  game itself is set to. The mod does not re-translate game content.
- **Never translated:** log lines. Developer-facing logs stay in English on
  purpose, so they read the same regardless of the player's language.

The mod picks the language automatically from the game's installed language
(detected from `dialog.tlk` at startup), so a German game speaks German cues, a
French game French, and so on. There is no in-game language switch yet.

## Currently supported languages

These are the five languages KOTOR 1 itself ships in, and the only ones the mod
can select automatically today:

- English (`en`)
- German (`de`) — the project's default and most complete
- French (`fr`)
- Italian (`it`)
- Spanish (`es`)

**Languages beyond these five are not supported yet.** Polish and Russian
translations already exist and will be supported in a future release, once the
manual language-override described below is in place (the game ships no Polish or
Russian `dialog.tlk`, so they can't be auto-detected the way the five native
languages are — see [How to add a new language](#how-to-add-a-new-language)).

## Where the strings live

All spoken strings are in the patch source, under
`patches/Accessibility/`:

- `strings.h` — the master list. One `enum class Id` with every string ID,
  grouped by feature with comments. **This is your reference for what each
  string is for** — the comments explain context, placeholders, and argument
  order.
- `strings_en.cpp` — the English table. Copy this as your starting point when
  adding a language; it is the canonical, comment-annotated set.
- `strings_de.cpp`, `strings_fr.cpp`, `strings_it.cpp`, `strings_es.cpp` — one
  file per language, each a single `Get(Id)` switch that returns the translated
  text for every ID.
- `strings.cpp` — the dispatcher that selects the active language. You normally
  don't touch this unless you're adding a brand-new language.

Each language file is the same shape:

```cpp
namespace acc::strings::lang_en {

const char* Get(Id id) {
    switch (id) {
        case Id::CategoryDoor:  return "Door";
        case Id::CategoryNpc:   return "NPC";
        // … one case per Id …
    }
    return "";
}

}  // namespace acc::strings::lang_en
```

To translate, you change only the **returned text** on the right. Never change
the `Id::` names on the left — those are the keys the code looks up.

## File format and encoding

The string files are C++ source, but for translation they read like a simple
list of `case Id::Something: return "text";` lines.

**Encoding — important for accented characters.** Source files use 7-bit ASCII
for English and **Windows-1252 hex escapes** for accented letters, so the bytes
match what the speech bridge expects. That means an accented character is
written as a hex escape, not typed directly. For example:

- `ü` → `\xFC`  (as in `"T\xFCr"` for "Tür")
- `à` → `\xE0`
- `é` → `\xE9`
- `ñ` → `\xF1`
- `ç` → `\xE7`

Look at the existing `strings_de.cpp` / `strings_fr.cpp` / `strings_es.cpp`
files for the pattern — every accented word in them is already written this way,
so you can copy the exact escapes. If you're unsure of a code, translate in
plain text and note in your PR "needs escaping"; a maintainer can convert it.

<!-- TODO (maintainer): if you later switch the build to /utf-8 and drop the
     hex-escape convention, update this section — plain UTF-8 source is much
     friendlier for translators. -->

## Format strings and placeholders

Some strings contain **placeholders** that the code fills in at runtime:

- `%s` — a piece of text (a name, an object label).
- `%d` — a number (a distance, a clock position, a count).

You must keep **the same placeholders** in your translation, but you can move
them to wherever your language's grammar needs. The **argument order is stable
across languages** — the first `%s` is always the same thing in every language,
the first `%d` the same number, and so on. The `strings.h` comment on each
format ID tells you what each placeholder is.

Example — the "announce object" format:

- English: `"%s, %d o'clock, %d metres"` → "Door, 3 o'clock, 5 metres"
- German:   `"%s, auf %d Uhr, %d Meter"` → "Tür, auf 3 Uhr, 5 Meter"

Both keep `%s`, `%d`, `%d` in the same order; only the connecting words differ.

**Rules:**

- Keep every `%s` and `%d` that the English string has — no more, no fewer.
- Keep them in the same relative order.
- Don't translate the placeholder itself (`%s` stays `%s`).

## How to improve an existing translation

1. Open the language file you want to fix (e.g. `patches/Accessibility/strings_fr.cpp`).
2. Find the `case Id::…:` line for the string.
3. Check the same `Id` in `strings.h` for the intended meaning and any
   placeholder notes.
4. Edit the returned text (mind the encoding for accented letters).
5. Send a PR. In the description, list which cues you changed and why (e.g.
   "'porte' was too terse; matched the game's own wording").

## How to add a new language

Adding a language touches five places. A maintainer can do the wiring if you'd
rather just supply the translated text — but here is the full checklist:

1. **`strings.h`** — add your language to `enum class Lang` (e.g. `Pt,`).
2. **`strings.h`** — add the forward declaration near the others:
   `namespace lang_pt { const char* Get(Id id); }`.
3. **`strings_pt.cpp`** (new file) — copy `strings_en.cpp`, rename the
   namespace to `acc::strings::lang_pt`, and translate every `return` value.
4. **`strings.cpp`** — add a `case Lang::Pt: return lang_pt::Get(id);` to the
   dispatcher `switch`.
5. **`core_dllmain.cpp`** — add your language to `DetectLanguageFromTlk()` and
   to the `LangName()` debug-name switch. Detection works by reading the
   `LanguageID` field (a little-endian int32 at byte offset 8) of the game's
   `dialog.tlk` header and mapping it to a `Lang`. The IDs BioWare uses are:
   `0 = English`, `1 = French`, `2 = German`, `3 = Italian`, `4 = Spanish`.
   Anything unrecognised falls back to English.

**Important caveat — languages beyond the game's five.** KOTOR 1 itself only
ships in English, French, German, Italian, and Spanish, so a stock `dialog.tlk`
only ever reports one of those five `LanguageID`s. That means a language outside
those five **cannot be auto-selected from a stock install** — there is no
`LanguageID` for it to detect. Such a language needs a *different* selection
trigger (a manual override / setting) rather than the `dialog.tlk`
auto-detection above, and the game's own dialogue and item names would still be
read out in whichever of the five languages the player's `dialog.tlk` is.

**Polish and Russian** are the concrete case here: translations for both already
exist and will ship in a future release, once the manual language-override lands.
They are not wired into `DetectLanguageFromTlk()` — that path would never fire
for them. If you want to contribute another language outside the five, open an
issue first so it can be hooked up to the manual override rather than to
auto-detection.

**Translated README (optional but nice).** If the new language should also get a
translated landing page, copy `docs/README.de.md` to `docs/README.<lang>.md`,
translate it, update its Jekyll front-matter (`title`, `permalink`) and the
in-body language switcher, and add the language to the switcher line in the main
`README.md` and the other `docs/README.*.md` files.

**Completeness matters:** the `Get` switch must return a value for *every* `Id`.
If you leave one out, that cue falls through to the empty-string fallback and
the user hears nothing. The easiest way to guarantee completeness is to start
from a full copy of `strings_en.cpp` and translate in place, so every case is
already present.

## String reference

There are ~650 string IDs. Rather than list every one here (they drift as the
mod grows), this is a **map of the feature groups** so you know what a cluster is
for and where to find it. The authoritative, always-current detail is the
**grouping comments in `strings.h`** — the enum is divided into ~75 labelled
sections (each starts with a `// ----` comment that names the feature, the
source file, and any placeholder meaning). To read a group, open `strings.h` and
search for the section name below; to translate it, edit the matching `case`
lines in your `strings_<lang>.cpp`.

A few conventions that apply throughout:

- **`Fmt…` IDs are format strings** with `%s` / `%d` placeholders. Keep every
  placeholder; the argument order is stable across languages (see
  [Format strings and placeholders](#format-strings-and-placeholders)).
- **Suffix IDs** (door-state, disabled-button, toggle-state, party status) are
  appended to another string, usually with a leading `", "`. Keep them short and
  make sure they read naturally *after* a name.
- **`KbName…` IDs** are the human-readable key names shown in the F1 key list and
  the keybind configurator — translate them the way your language names those
  actions in menus.

### World navigation and orientation

- **Object categories** (`CategoryDoor`, `CategoryNpc`, `CategoryContainer`,
  `CategoryItem`, `CategoryLandmark`, `CategoryTransition`, `CategoryMapHint`) —
  the category prefix spoken in the discovered-object cycle. Mind singular vs.
  plural: these are singular; the empty-state messages below use the plural.
- **Empty-category messages** (`EmptyDoors`, `EmptyNpcs`, … `EmptyAll`,
  `CycleNoTarget`) — full "no X in range" phrases; each is written out so the
  plural is correct.
- **Per-item announce templates** (`FmtAnnounceWithClock`, `FmtAnnounceNoClock`,
  `FmtCategoryItem`) — how an object is read out: name, clock direction, metres.
- **Compass / heading** (the octagonal direction names, stuck-direction probe,
  exact-heading announce) — spoken for the AltGr facing cue and the N turn key.
- **Transitions** (`Pillar 2 transitions` group) — room and area change
  announcements; single `%s` for the destination name.

### Movement guidance — autowalk, beacons, routes

- **Guidance / autowalk** (`FmtGuidingTo`, `FmtGuidingFailed`, `GuidanceNoFocus`,
  `GuidingToPoint`, `MovementCancelled`, `InteractWayBlocked`, …) — Shift+- walk
  cues and their failure states.
- **Beacon** (`FmtBeaconStarted`, `BeaconCancelled`, `FmtBeaconNoPath`, …) —
  Ctrl+- audio-beacon cues.
- **Route descriptions** (`FmtRouteHeader`, `FmtRouteSegment`, `RouteJoinSeparator`,
  `RouteOneTransition`, `RouteNoTransition`, `FmtBeaconNextSegment`) — the spoken
  turn-by-turn route. `FmtRouteSegment` is `"%d metres %s"` (distance +
  direction); `RouteJoinSeparator` is the `", "` between segments.

### World interaction and actions

- **Combined autowalk+interact (Enter)** and **engine-picked action pre-roll** —
  what's spoken when Enter acts on the narrated target.
- **Radial / action-bar** groups (`Player action bar` armed by Shift+4..7,
  empty-radial announcements, generic tooltip fallback) — the action surfaces.
- **Interaction verbs** (`FmtInteractTalk`, and the talk/attack/open/use family)
  — action labels; usually `"%s"` for the target name.
- **Sealed-door / softlock guidance** (sealed-door override, Endar Spire
  `end_door16`) — area-specific spoken hints.

### Map and markers

- **In-game map UI** (`In-game map UI` group, ~20 IDs) — map cursor readouts,
  terrain/marker announcements, note navigation.
- **Map pins & saved markers** (`MapPinNoText`, `MapPinInteractHint`,
  `FmtSavedMarkerAutoNumber`, `FmtSavedMarkerAutoWithRoom`, `FmtSavedMarkerPlaced`,
  `SavedMarkerFailed`) — Shift+N pin naming and activation cues.
- **Galaxy / star map** (`Galaxy / star map travel screen` group) — planet
  travel screen.

### Menus and screens

Each of these is one game screen; the group comment names its `.gui`.

- **Unified action menu** (category names + labels) — the Shift+Enter menu.
- **Character creation** (`Chargen` portrait, attribute, skills, feats/talents,
  and layout groups) — the new-character wizard.
- **Character sheet & level-up** (`Character sheet` opener, `Level-up` hotkey and
  step ordering, `menus_powers_levelup`) — the P screen and Shift+L flow.
- **Equipment & inventory** (`Equipment screen`, equip sub-screen name, virtual
  credits row) — the U / I screens.
- **Container loot** (`Container loot panel` group) — take-all / give panel.
- **Store / trading** (`Store` group + per-trade speech with and without price)
  — buy/sell.
- **Workbench** (`Workbench` slot groups) — the upgrade bench.
- **Journal & messages** (quest-items button, messages-panel review titles) — L
  and J screens.
- **Save / load** (`Save / Load game panel` group).
- **Options** (`Sound options` label fix-up, slider value readout, keyboard-mapping
  screen) — the O screens.
- **Shared element-state suffixes** (disabled-button, toggle/checkbox state,
  slider value, party-selection status) — appended to control names; keep them
  short.

### Health, combat, examine

- **Combat mode** (entry/exit announcement, bare-H leader-status fallback) — F /
  combat toggle cues.
- **Self status** (`Bare-H self status`, HP opener) — the H key readout.
- **Opponent cycle** (`Combat system … opponent cycle` group) — target callouts.
- **Attack & saving-throw callouts** (`per-attack resolved callout`,
  `saving-throw callout`) — spoken combat results.
- **Action queue** (`action queue submenu` group) — the Shift+H queue.
- **Examine view** (`Examine view` groups, ~27 IDs) — the Ö examine panel: a
  navigable list of a target's stats, effects, and equipment. This is one of the
  larger groups; the effect names also have per-language tables in
  `examine_view.cpp`.
- **Dialog** (`Dialog screen — reply availability cue`) — spoken when replies
  are/aren't available.

### Help system and key names

- **Help system** (`Help system` group, ~66 IDs) — the F1 key list and the
  per-screen "keys for this screen" surface. Large group; many entries are the
  descriptive text for each key.
- **Key names** (`KbName…` IDs, e.g. `KbNameCheckForUpdate`) — the action names
  shown in the key list and the mod's keybind configurator
  (`Mod-settings → Tastenbelegung` group).

### Mod settings

- **Mod-settings submenu** (`Mod-settings virtual submenu` group) — the mod's own
  options screen (labels, toggles, values).
- **Audio glossary** (`Mod-settings → Audio glossary` group) — the descriptions
  of what each audio cue means.

### Minigames

- **Pazaak** (`Pazaak minigame` group, ~47 IDs) — the card board: hands, tables,
  totals, stand/end-turn, the plus/minus sign chooser, the wager screen, and the
  side-deck builder (`menus_pazaakdeck`). The largest minigame group.
- **Turret** (`Turret … minigame` group) — the space-combat gunner cues.
- **Swoop** (`Swoop racing minigame` group) — the racer cues.
- **Area puzzles** (`Rakatan temple floor-plate puzzle`) — the floor-plate
  puzzle prompts.

### Tutorial hints and system

- **Endar Spire tutorial hints** (`Endar Spire tutorial keyboard hints` group,
  ~42 IDs) — the custom keyboard tutorial that replaces the vanilla popups on the
  opening ship. Text-heavy; these are full instructional sentences.
- **Auto-updater** (`In-game auto-updater` group: `Update available`,
  downloading, no-update, version readout) — the F5 update cues.
- **Panel-title overrides & bringup nag** — small system/diagnostic strings.

## Submitting your translation

1. Fork the repo and create a branch.
2. Edit or add the language file under `patches/Accessibility/`.
3. If you can build, compile once to confirm there are no syntax errors
   (a stray missing `"` or `;` will break the build). If you can't build, say
   so in the PR — a maintainer will compile and test.
4. Open a pull request describing which language and which cues you touched.
5. A maintainer tests the cues in-game with a screen reader before merging —
   translations can't be verified by compilation alone.

## Getting help

Unsure what a cue means or how a sentence should read in context? Open an issue
or ask in your PR. Context questions are welcome — a literal translation that
ignores how the cue is actually used is worse than asking first.
