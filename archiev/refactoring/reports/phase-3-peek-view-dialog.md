# Phase 3 scan — object peeking, view mode, dialog speech, tutorials and help

Scope:
- peek_description.cpp (811 lines), peek_description.h (36)
- view_mode.cpp (743), view_mode.h (56)
- dialog_speech.cpp (749), dialog_speech.h (24)
- tutorial_hints.cpp (293), tutorial_hints.h (63)
- tutorial_popup.cpp (222), tutorial_popup.h (42)
- passive_narrate.cpp (483), passive_narrate.h (49)
- help.cpp (441), help.h (54)

Method: full read of all 14 files (not excerpted). For every `#include`,
grepped the qualified symbol / bare function name actually used in the
including file (not the header name) before calling anything dead, per the
brief's Trap 1. For raw hex literals, grepped `engine_offsets*.h` and the
other project headers for an existing named constant at the same value
before flagging duplication. For every candidate SEH-guard gap, located the
sibling function doing the same kind of read and confirmed whether it
guards, by direct comparison of the two function bodies. For the
`acc::strings::Get()` null-check question, read `strings.cpp:15-25` — `Get`
switches on language and falls back to `""`, never `nullptr` — and then
grepped every `acc::strings::Get(` call site in the batch for a `s &&` /
`!t ||` style guard around it.

## Section A — general low-level cleanup

### A1 — Dead `#include "engine_manager.h"` (tutorial_popup.cpp:6)

`tutorial_popup.cpp` includes `engine_manager.h` with the comment
`// kAddrGuiManagerPtr, kMgrPanels*`, but neither `kAddrGuiManagerPtr` nor
any `kMgrPanels*` constant appears anywhere in the file (`grep -n
"kAddrGuiManagerPtr\|kMgrPanels" tutorial_popup.cpp` matches only the
`#include` line itself). The file's panel lookup goes through
`acc::engine::FindPanelByKind` (from `engine_panels.h`, already included)
instead. This reads like a leftover from before the file was switched onto
`FindPanelByKind`.

- Proposed change: delete the include line.
- Risk: mechanical (compiler-checked — a stray real dependency would fail
  to build).
- Estimated line delta: -1.

### A2 — `view_mode.cpp`'s local `kHoverPauseMs` shadows the published constant it duplicates (view_mode.cpp:38, view_mode.h:27)

Phase 2 (B4-B6, per STATE.md) deliberately published
`acc::view_mode::kHoverPauseMs = 300` from `view_mode.h` so
`map_ui_cursor.cpp` could share the same hover-pause cadence instead of
re-declaring its own 300. `view_mode.cpp` itself, however, still carries its
own anonymous-namespace copy:

```cpp
constexpr DWORD kHoverPauseMs = 300;          // settle time before speaking
```

Because this is nested inside `namespace acc::view_mode { namespace { ... } }`,
unqualified lookup at the two use sites (`view_mode.cpp:399` and `:477`)
resolves to this local copy, not the header's `acc::view_mode::kHoverPauseMs`.
Values happen to match today, so there's no live bug, but the constant is no
longer actually "published and shared" from this file's own point of view —
if someone changed the header value expecting it to retune view mode's
hover pause too, it silently wouldn't. This is not a proposal to move the
constant back to the .cpp (the brief's do-not-touch item) — it's the
opposite: the .cpp's redundant local copy should go, so the file actually
uses the header constant it exports.

- Proposed change: delete the local `constexpr DWORD kHoverPauseMs = 300;`
  line; the two use sites resolve automatically to
  `acc::view_mode::kHoverPauseMs` since the file is already inside that
  namespace.
- Risk: mechanical (value-identical; compiler resolves the qualified name
  automatically since we're already inside the namespace).
- Estimated line delta: -1.

### A3 — Raw hex literal duplicates an existing named constant (peek_description.cpp:208)

```cpp
{ acc::engine::PanelKind::Store, 0x1a40, RefreshStore },  // CSWGuiStore.description_listbox
```

`engine_offsets_fields.h:1013` already names this exact value:

```cpp
constexpr size_t    kStoreDescriptionListBoxOffset         = 0x1a40;
```

Every other row in the same `kPanels[]` table (peek_description.cpp:204-214)
uses a raw literal too — but only this one duplicates an existing named
constant; the others (Inventory 0x0844, Journal 0x01a4, Abilities via
`kAbilitiesDescListBoxOffset`) have no such match, so this is the one
concrete instance, not a pattern to sweep the whole table for.

- Proposed change: replace `0x1a40` with `kStoreDescriptionListBoxOffset`.
- Risk: mechanical (identical value, compiler-checked).
- Estimated line delta: 0.

### A4 — Orphaned comment continuation in dialog_speech.cpp's include block (dialog_speech.cpp:19-23)

```cpp
#include "transitions.h"      // IsModuleLoadPending — gate during cutscene-load
#include "tutorial_hints.h"   // HintForDialogLine — detect a rewritten tutorial line
#include "locked_recall.h"    // MaybeCapture — story-locked-object bark recall
#include "tutorial_popup.h"   // RecordPendingHint — fire a popup at the reply break
                              // transient (engine LYT loader use-after-free)
```

Line 23 is a comment-only continuation line with no `#include` of its own,
aligned to the same column as the trailing comments above it, floating three
lines below the `transitions.h` line it actually explains (why
`IsModuleLoadPending` gates dialog polling: a transient engine LYT-loader
use-after-free during cutscene handoff). This reads as debris from a
reordering edit that moved the include lines around but left this
continuation behind at its old position.

- Proposed change: move `// transient (engine LYT loader use-after-free)`
  up to directly follow the `transitions.h` comment it belongs to (line 19),
  or fold it into that comment.
- Risk: mechanical (comment-only, zero behaviour change).
- Estimated line delta: -1 (net, after merge).

### A5 — `PendingContainsHint` has external linkage for no reason (tutorial_popup.cpp:157)

Every other file-local helper in `tutorial_popup.cpp` (`SetPause`,
`TutorialBoxPresent`, `FirePopup`) lives inside the anonymous namespace
(lines 13-151). `PendingContainsHint` is defined right after that namespace
closes, at `acc::tutorial_popup` scope with external linkage, even though
it's declared in neither `tutorial_popup.h` nor called from any other file
(`grep -rn "PendingContainsHint" .` outside this file returns nothing). It's
used only by `RecordPendingHint` a few lines below it, in the same file.

- Proposed change: move `PendingContainsHint`'s definition inside the
  anonymous namespace block, alongside the file's other internal helpers.
- Risk: mechanical (no external callers, confirmed by grep; pure move).
- Estimated line delta: 0.

### A6 — Locally re-declared address constants duplicate existing named ones elsewhere (tutorial_popup.cpp:59, 61)

```cpp
constexpr uintptr_t kAddrAppManagerPtr  = 0x007A39FC;
...
constexpr uintptr_t kAddrExoSoundPtr    = 0x007a39ec;
```

Both addresses already have canonical named constants elsewhere in the
codebase: `engine_player.h:195` defines `kAddrAppManagerPtr` (same name,
same value) and `audio_bus.h:107` defines `kAddrCExoSoundPtr` (same value,
different name) for the same `.data` global. The file's own comment at
line 53 ("Pause (mirrors engine_subscreen; kept self-contained here)")
explicitly justifies duplicating the *pause-toggle mechanism* to avoid
coupling this small feature to bigger files — but that rationale doesn't
obviously extend to hand-copying the two raw addresses under a
name-for-name identical constant (`kAddrAppManagerPtr`) rather than just
including the header that already names them. Flagging for the user's
judgment rather than proposing outright, since pulling in
`engine_player.h` here would add a real (if header-only) dependency the
author may have wanted to avoid.

- Proposed change (if wanted): replace the two local `constexpr` lines with
  `#include "engine_player.h"` (for `kAddrAppManagerPtr`) and
  `#include "audio_bus.h"` (for `kAddrCExoSoundPtr`), dropping the local
  duplicates.
- Risk: low (adds two header dependencies to a currently light file; values
  are identical so behaviour is unchanged).
- Estimated line delta: -2, +0 (two new includes, likely already
  transitively pulled in by existing includes — unverified).

### A7 — `dialog_speech.cpp::Tick()` mixes four separable jobs in one ~200-line function (dialog_speech.cpp:547-747)

`Tick()` does, in sequence: (1) find + track the main dialog panel and speak
new NPC lines with the human-subtitle/skill-check-marker logic, (2) for the
Computer dialog variant, speak newly appended terminal rows, (3) handle the
R-repeat hotkey, (4) find + track the bark bubble panel and speak new bark
text. Blocks 1-3 share the `m.panel` / `s_lastNpcLine` state; block 4 is
independent (a bark bubble has its own lifecycle, per the file's own
header comment) and uses entirely separate statics
(`s_lastBark`, `s_lastBarkText`). This is exactly the kind of
function-level decomposition the brief calls in-scope for Phase 3.

- Proposed change: extract block 4 into a `TickBarkBubble()` helper (and
  optionally blocks 1-3 into `TickDialogPanel()`), both called from `Tick()`.
  The function-local statics that currently live inside `Tick()`
  (`s_lastPanel`, `s_lastNpcLine`, `s_lastComputerRows`, `s_lastBark`,
  `s_lastBarkText`) would move to the anonymous namespace at file scope —
  no semantic change, since function-local `static` already gives them
  the same effective lifetime.
- Risk: low — pure extraction, no dependency crosses the two halves (only
  the standalone bark-bubble state does, and it stays with the bark half).
  Recommend an in-game check: a short dialogue with at least one bark
  bubble (e.g. Trask's Endar Spire intro), confirming both NPC-line and
  bark speech still fire and the R-repeat hotkey still works.
- Estimated line delta: ~0 (restructure, not a size reduction).

## Section B — AI-pattern findings

### B1 — `CategoryNameId()` is byte-identical in two files with no shared home (view_mode.cpp:300-313, passive_narrate.cpp:112-125)

Both files independently define the exact same helper:

```cpp
acc::strings::Id CategoryNameId(acc::filter::CycleCategory c) {
    using C = acc::filter::CycleCategory;
    using S = acc::strings::Id;
    switch (c) {
        case C::Door:       return S::CategoryDoor;
        case C::Npc:        return S::CategoryNpc;
        case C::Container:  return S::CategoryContainer;
        case C::Item:       return S::CategoryItem;
        case C::Landmark:   return S::CategoryLandmark;
        case C::Transition: return S::CategoryTransition;
        case C::Count_:     break;
    }
    return S::CategoryItem;
}
```

`filter_objects.h` already publishes the sibling mapping,
`CategoryName(CycleCategory)` (a `const char*` for log lines) — but the
localized-`strings::Id` mapping used for user-facing fallback names exists
nowhere shared; both files reinvented it as a private anonymous-namespace
helper. (`cycle_state.cpp` and `interact_dispatch.cpp` only use the
log-only `CategoryName`, not this one.)

- Proposed change: move `CategoryNameId` into `filter_objects.h`/`.cpp` next
  to `CategoryName` (e.g. as `acc::filter::CategoryNameId`), and have both
  `view_mode.cpp` and `passive_narrate.cpp` call the shared version instead
  of their own copy.
- Risk: low — logic is byte-identical between the two existing copies, so
  consolidating cannot change either call site's behaviour. Worth a quick
  in-game spot check of the fallback path itself (an object with no
  resolvable name, in view mode and via passive ShowObject) since it's the
  one path this touches.
- Estimated line delta: -14 (net, after adding one shared definition and
  removing both private copies).

### B2 — Dead null checks on `acc::strings::Get()` (tutorial_hints.cpp:148-149, 215-216, 271; help.cpp:301-302)

`acc::strings::Get(Id)` (strings.cpp:15-25) switches on the active language
and falls through to `return "";` — it never returns `nullptr`. Four call
sites in this batch still guard against it:

```cpp
// tutorial_hints.cpp:148-149
const char* s = acc::strings::Get(id);
return (s && s[0]) ? s : nullptr;

// tutorial_hints.cpp:215-216 (same shape)
// tutorial_hints.cpp:271
if (!s || !s[0]) return nullptr;

// help.cpp:301-302
const char* t = acc::strings::Get(kEntries[e].label);
if (!t || !t[0]) continue;
```

The `s[0]` / `t[0]` empty-string check is legitimate (an unmapped id maps
to `""`); only the `s &&` / `!s ||` half is dead.

- Proposed change: drop the null half of each condition —
  `return s[0] ? s : nullptr;` and `if (!t[0]) continue;` (etc.).
- Risk: mechanical — `Get()`'s only return paths are the six
  `lang_xx::Get(id)` calls and the literal `""`, none of which can produce
  `nullptr`, confirmed by reading strings.cpp directly.
- Estimated line delta: 0 (four one-line simplifications).

## Findings (possible bugs — user decides)

### 1 — `dialog_speech.cpp::FindActiveDialogPanel` reads the GUI manager's `panels[]` without the SEH guard its own file's other panel-walks use, and without the guard its closest sibling function uses (dialog_speech.cpp:470-496)

```cpp
DialogPanelMatch FindActiveDialogPanel() {
    DialogPanelMatch out{nullptr, acc::engine::PanelKind::Unknown};
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return out;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return out;
    int n = panelCount > 16 ? 16 : panelCount;
    for (int i = 0; i < n; ++i) {
        void* p = panelData[i];
        ...
```

None of the `panelCount` / `panelData` / `panelData[i]` reads are inside a
`__try`/`__except`. `dialog_speech.cpp`'s own `Tick()` comment, directly
above the call to this function, explains exactly why that's risky:

> Module-load latch — FindActiveDialogPanel walks the manager's panels[]
> which the engine is tearing down mid-handoff during a cutscene
> transition.

The `IsModuleLoadPending()` check gates the *known* teardown window, but is
a state latch, not a guard against the read itself faulting for any other
reason. Every other panel-reading helper in this same file —
`ReadListBoxRowCount`, `ReadFirstVisibleText`, `FillSpeakerFromServerObject`,
the inline computer-terminal read in `Tick()` — wraps its reads in
`__try`/`__except`. More directly: the near-identical panels[]-walk in
`engine_panels.cpp::FindPanelByKind` (lines 782-804) — which this very file
calls one screen-section later via `FindBarkBubblePanel()` — DOES guard the
same `panelCount`/`panelData` read:

```cpp
// engine_panels.cpp:787-794
int    panelCount = 0;
void** panelData  = nullptr;
__try {
    panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    panelData  = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
} __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
}
```

`FindActiveDialogPanel` exists as its own hand-written scan (rather than
using `FindPanelByKind`) because it needs to match any of four dialog
panel kinds in one pass, which `FindPanelByKind`'s single-kind signature
doesn't support — that part is a reasonable reason for the duplication to
exist. But the missing guard on the duplicated logic looks like an
oversight, not a deliberate difference. This is the third instance across
the sweep of an engine-memory read missing the SEH guard its sibling
function uses.

- Proposed change: wrap the `panelCount`/`panelData` reads (and, ideally,
  the `panelData[i]` deref in the loop) in `__try`/`__except`, returning the
  empty `DialogPanelMatch` on fault — matching `FindPanelByKind`'s shape
  exactly.
- Risk: needs-in-game-test for confirming the underlying bug actually
  manifests (the fix itself is mechanical — same guard shape as the
  adjacent, working `FindPanelByKind`). Exercise by walking away from an
  NPC mid-dialogue right as a scripted cutscene/area handoff fires (the
  Endar Spire intro and the Taris apartment escape both have
  dialogue-into-cutscene transitions), and by talking to someone right as
  a save auto-triggers a module boundary. Both are moments the module-load
  latch may not fully cover.

## Candidate 28 — narrow-header include opportunities

- `view_mode.h:15` and `view_mode.cpp:18` — both include the full
  `engine_offsets.h` aggregator solely for `Vector` (published from
  `engine_offsets_types.h`). Everything else this pair of files uses
  (`kClientOptionsBitFieldOffset`, `kMgrModalStackSizeOffset`, etc.) already
  comes from `engine_options.h` / `engine_manager.h` directly. Clean
  narrowing candidate: swap to `engine_offsets_types.h`.
- `dialog_speech.cpp:10` — includes the aggregator but only needs
  `engine_offsets_fields.h` (every named offset constant used in the file)
  plus `engine_offsets_types.h` (for `CExoArrayList`). Narrowing candidate:
  swap to those two.
- `peek_description.cpp:5` — includes the aggregator and needs three of its
  four pieces: `engine_offsets_types.h` (`CExoArrayList`),
  `engine_offsets_fields.h` (the bulk of the constants used), and
  `engine_offsets_addresses.h` (`kAddrCSWGuiUpgradeOnControlEntered`). Only
  `engine_offsets_values.h` is unused, so narrowing here buys very little —
  noted for completeness, not recommended as worth doing on its own.

## Files scanned with nothing to report

- peek_description.h
- tutorial_hints.h
- tutorial_popup.h
- passive_narrate.h
- help.h
- dialog_speech.h
