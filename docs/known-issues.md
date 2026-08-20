# Known Issues

Status tracker for accessibility-mod work, in five buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Unreproduced** — reported issues we can't reliably reproduce yet; need a repro before fixing.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

### Chain drives a freed panel when its address is reused — fix built 2026-08-20, awaiting test

Two KOTOR 2 chargen runs crashed while arrowing the Fähigkeiten list
(logs `patch-20260819-063818` / `patch-20260819-221733`, dump
`swkotor2.exe.3664.dmp`). The access violation is in the engine, inside
`CSWGuiSkillsCharGen::IsClassSkill`'s K2 twin, dereferencing the panel's
`chargen_creature` (`+0x68`) — a field the constructor sets from its
`param_2` and nothing rewrites.

Root cause is NOT a chargen binding problem. The chain was still bound to
the Attribute panel after the engine freed it: its buttons dispatched with
`is_active` reading 2622030025 (freed memory), and the Skills panel was
then allocated onto the same block, which is why it appeared at the
Attribute panel's old address with a half-written creature field. The
engine's own handlers then walked that field and died.

`ValidateChainPanel` could not catch it: it tests membership in `panels[]`,
which the address passes again as soon as a new panel occupies it.

Evidence it is address reuse and not a chargen invariant: the healthy run
`patch-20260820-105008` shows the Skills panel at its own address with
`+0x68` valid and stable, costs resolving (Preis 2 / 2 / 1), descriptions
and remaining points all correct — on the same screen the user has used
for months.

Fix: `ChainPanelIdentityHolds` records the panel's vtable at RebindChain
time and compares it before anything drives that panel — the FireActivate
dispatch, the cursor warp, and the chain step (which now rebinds instead).
Trips loudly as `ChainIdentity STALE`.

Known gap: reuse by a panel of the SAME class passes the vtable compare.
That needs a generation token; the vtable version landed first because it
is falsifiable on the known repro.

Two dead ends worth not repeating: the crash is not a transient
init race (the field stayed zero for 7s), and gating our announcements on
a "binding ready" probe is wrong — it suppressed cost, description and
warp on healthy panels while fixing nothing, because the probe was reading
a symptom of the recycled memory.

## Unreproduced

### Dead keyboard on a machine with no mouse — fix built, awaiting a no-mouse session

kenny's KOTOR 2 0.7.5 session (`userlogs/kenny075keyboarddeath.7z`) reached the main
menu with speech and cursor working and never delivered one keystroke to the engine.
Root cause is the engine's own DirectInput teardown: any failure creating the mouse
device releases the keyboard device too, and nothing rebuilds it — see
`docs/llm-docs/gui-and-input-internals.md` → `directinput_device_lifecycle`.

The fix (mouse-teardown block + per-frame retry latch + a DirectInput state line in
the log) is in and verified on the HEALTHY path in both games: the block installs
(5 call sites on KOTOR 2, 2 on KOTOR 1) and the state probe reports the interface,
keyboard and mouse correctly through startup and focus loss.

What is NOT verified is the failure path itself, because this dev machine has a
mouse and DirectInput creates the device normally. Unplugging a USB mouse may not
reproduce it either — the failing step could be any of CreateDevice, SetDataFormat,
SetCooperativeLevel, SetProperty or Acquire, and the handler now names which one in
the log. Confirmation has to come from kenny's next session: expect either a
`MouseTeardownBlock: mouse bring-up failed at <step>` line with a working keyboard,
or no such line at all (in which case the teardown comes from somewhere else and the
`DirectInput state` lines will show where).

## Planned

### Beacon-active navigation announcements (remaining-route reading)

While a beacon (or autowalk) is active, the route announcements should describe the *remaining* way to the active waypoint — leading with the range and direction of the current target — rather than the full original route. The hard part is disambiguating intents: if the player selects another object just to hear where it is, the announcement must not balloon into the long, confusing full-route description for the beacon target. Design questions to resolve: how to keep "where is this thing I just selected" reads short while a beacon is running, and whether the separate Shift+Enter autowalk / Shift+`-` gestures are still needed at all, or whether they can be folded into / replaced by the plain selection + beacon flow. See `project_narrated_target_unified.md` and `project_map_cycle_architecture.md`.

### Nameable personal map pins

Let players give their own map pins a name when they drop one, and read that name back when cycling to the pin. Personal pins currently announce only a generic label plus position, so a player who marks several spots can't tell them apart. Needs a text-entry path that works from the keyboard with a screen reader (the editbox handling shipped for chargen + save naming — `menus_editbox.cpp` — is the closest existing mechanism, though map pins aren't an engine editbox so the entry surface differs), storage of the label alongside the pin in the save, and the cycle/focus announcements updated to speak it. See `project_narrated_target_unified.md` / `map_user_markers.cpp`.

### Bundle the resolution / widescreen patch — once we are out of beta

Offer the community resolution / widescreen + HR-menus patches as an installer option, and make our own code robust to the layout changes they introduce. Deliberately deferred until the mod leaves beta: it widens the support surface (several of our paths still assume a particular screen geometry — screen-pixel coordinates, the OS cursor, hardcoded hit-test offset compensations), and a beta is the wrong time to add a second variable to every bug report. Prerequisite when we do take it on: audit every place we depend on screen geometry and read it live from the engine instead of assuming. See `docs/installer.md` (Widescreen / HR-menus bundling) and the `project_radial_cursor_coupling` memory.

## Monitor

### Grass crash — worked around with `Grass=0`, root cause not fully pinned

Shipped in 0.7.7: the installer sets `Grass=0` in `[Graphics Options]` on both games, which removes the faulting code path. Full analysis in `docs/grass-crash-analysis.md`.

What is confirmed: the fault is an access violation inside the Intel OpenGL driver, called from `GLRender::DrawLightmappedGrass`; grass is always submitted as client-side vertex arrays; and `DrawLightmappedGrass` derives its attribute base as `param_1 + param_6 * 0x30`, which is only correct when `param_6` is the bin's *total* quad count — so the per-sub-bin draw loop in `RenderGrassPolys` feeds the driver attribute pointers aliasing into the position block.

What is **not** explained: that defect reads wrong data but stays inside the allocation for every stride the engine uses, so it does not by itself produce the page fault. Most likely remaining candidate is a sub-bin count list disagreeing with `field6_0xc`. `CAurTriangleBin` is a Ghidra PlaceHolder struct, so per-field reasoning is unreliable until it is re-typed.

Watch for: any grass-area crash report from a user who has re-run the 0.7.7 installer (would mean `Grass=0` does not gate the path as assumed — we traced the option to the `enableGrass` global but never traced which caller reads it).

### Tick-clock handling is inconsistent across the codebase; a mass change may be needed

A tick is one rendered **frame** — `core_tick` fans out from a detour on `CSWGuiManager::Update`, called once per frame from the engine main loop — so the interval between ticks is whatever the framerate is: ~16.7 ms at 60 Hz vsync, ~6.9 ms at 144 Hz, less with vsync off. There is no fixed tact to assume.

Until 0.7.3 nothing published a tick timestamp. `core_tick` was built as a dispatcher (fan-out ordering) and its `QueryPerformanceCounter` reads were private to the watchdog's SLOW FRAME / SLOW TICK diagnostics, so all 132 clock reads across 42 files call `GetTickCount` independently, at ~15.6 ms resolution. `acc::tick::NowMs()` now publishes the tick-start stamp (same QPC read the watchdog already takes, so it costs nothing extra), but only `camera_announce` consumes it.

**Why this was not caught by any duplication sweep:** there is no duplicated helper to find. A `GetTickCount()` call looks identical and idiomatic at every site; what distinguishes a safe use from an unsafe one is what the caller does with the subtraction two lines later, which no naming or structure pass inspects.

Sorted by what the clock is used for:

- **Deadlines** ("have N ms passed") — the large majority, and correct as-is: one quantum of error against a window of hundreds of ms. No migration needed.
- **Integration** (`speed × dt` — `map_ui_cursor`, `view_mode` cursor stepping) — also correct, because both update their last-stamp unconditionally, so a zero-length tick contributes nothing and the next tick's dt covers the gap; the dts still sum to real elapsed time.
- **Differentiation** (`rate = delta / dt`) — the only unsafe pattern, because a near-zero interval does not degrade a rate, it falsifies it. Three sites: `camera_announce` (fixed in 0.7.3 — it reported 0°/s mid-turn and fired three spurious stop cues in one second), `audio_footstep_suppress` (divides over a ≥500 ms window, ~3% error, fine), and `camera_spin_guard` (per-tick yaw delta; a `dt > 0` guard prevents a divide-by-zero but a zero-length tick reports 0°/s, so the edge-spin guard can skip a correction tick — shipped behaviour, self-correcting next frame, not yet fixed).

**Open question:** whether to migrate more broadly or leave the deadline sites alone. Current judgement is to leave them — they are correct, and a mass change would be a lot of untested behaviour for no benefit. Revisit if a third differentiation site appears, or if a high-refresh-rate tester reports timing-dependent oddities.

**Constraint on any migration:** `NowMs()` is the *current tick's* timestamp, not a live clock. It is only valid inside the Dispatch fan-out; code reached from an input hook, a menu callback or a hotkey runs outside the tick and would read a stale value.

### KOTOR 2 does not refuse impossible actions — we announce their absence instead

A medkit used on a full-health character behaves differently in the two engines, and only one of them tells the player anything.

KOTOR 1 refuses it at the GUI layer: `DoPersonalAction` finds the entry's usable bit clear, plays its error sound, and never dispatches. The entry's flag word carries a reason code, so we speak the engine's own sentence — logged as `ActionBar: variant flags ... usable=0 reason=5` followed by `refused — speaking engine reason strref=0xa602 [Volle Gesundheit]`.

KOTOR 2 mostly cannot: the answer to this entry's original open question (decompiled 2026-08-12, the dead-Medicine-row investigation) is that K2's item appender **never initialises the flag word for inventory items** — only the three equipped-slot entries get one. What we read there is heap garbage (ASCII string fragments in the logs), the engine gates on the same garbage, and `VariantRefusal` therefore skips the refusal verdict for K2 item entries entirely (a garbage bit would fake a refusal with a random reason). Force/feat entries DO compute real flags on K2 — "Macht erschöpft" (`usable=0 reason=1`) was observed live — so the mechanism exists, it is just never fed for items. The no-op watch stays as the honest answer for silent item refusals.

**The K2 Medicine row needed a deeper fix (2026-08-12).** K2 gives every medical-category item a TWO-STEP use flow: the press arms a target-pick (MainInterface `+0x15230` armed / `+0x15234` party slot) that only a mouse click on a party portrait completes — and `DoPersonalAction`'s preamble wipes the state before the medical handler reads it, so from the keyboard the row can never fire at all (one press wedges it armed-with-no-slot; the 2026-08-11 21:58 "flip" in this playthrough was simply the first medical press whose base-item row byte routes into the two-step flow). The shipped answer bypasses that machinery: `SendUseItemRequestK2` emits the same client→server use-item message the portrait click's consume path emits, and the unified menu adds a party-member picker (narrated member preselected, Shift+Enter = instant self, bare 5 = instant self). The `Diag.UseItemReq` hook on the message writer doubles as the no-op watch's cancel signal: a sent request proves the press acted, which also fixed mines announcing "Nicht möglich" while visibly planting (they act without a combat-round add).

**One false-positive class found and fixed (2026-08-08).** The watch was armed for every menu fire, and target-row actions tripped it three for three on a footlocker: Sicherheit, Sicherheits-Überbrücker, and a Schallmine that visibly blew the lock open all announced "Nicht möglich" while working perfectly (`patch-20260808-103232.log`). Target rows act without a combat-round add as a matter of course, so the watch is now armed only for the personal block — `ArmNoOpWatch`, called from the menu's personal path and from bare 4..7, never from `ArmUserQueueAdd` itself.

**Still watching:** KOTOR 2's Combat Behaviour column (key 8) may legitimately queue nothing — if it does it will wrongly announce "Nicht möglich" and needs the same send-witness treatment. And a server-side drop of a direct-sent medical item (e.g. full health) is silent-then-generic rather than reasoned — the engine's client-side CanUseItem gate (`0x005D2F80`) is skipped by the direct send and could later supply real reasons.

Code: `engine_actionbar.cpp` (`VariantRefusal`, `SendUseItemRequestK2`, the always-on flags log), `combat_queue.cpp` (`ArmUserQueueAdd` / `TickNoOpWatch`), `combat_diag.cpp` (`OnUseItemRequestK2`), `unified_action_menu.cpp` (`SpeakRefusal`, the medical party picker).

## Polish

### Starting an autowalk/beacon while one is active should switch, not cancel

If an autowalk or beacon is already running and the player triggers a new autowalk or beacon on a different target, the current behaviour just cancels the existing one. Instead it should immediately start the new action on the new target — switching the route in one gesture rather than requiring cancel-then-start.

### Mod-settings sliders only save after pressing Enter

The sliders under Mod-Einstellungen (e.g. hint-sound volume) only persist their new value to `acc_settings.ini` once the user presses Enter on the row. Adjusting a slider with Left / Right changes it for the session and previews the new level, but the change isn't written until an explicit Enter — so a player who tweaks a slider and leaves the menu without pressing Enter loses the change on next launch. Slider adjustments should persist on each Left / Right step (the same way the value already updates live), without needing a confirming keypress.

### Fold in human translation contributions as they arrive

Most of the mod's non-German, non-English text is machine-drafted — the in-game strings for French, Italian, Spanish, Russian and Polish, and the installer's `Locales/{fr,it,es}.json`. They are correct in structure (key parity verified) but the phrasing, formal-vs-informal address, and modding terminology may read awkwardly to a native ear. German is human-authored by the maintainer and is the quality bar. As native speakers contribute corrections, merge them; PRs against the individual string files are a well-scoped contribution to point people at.
