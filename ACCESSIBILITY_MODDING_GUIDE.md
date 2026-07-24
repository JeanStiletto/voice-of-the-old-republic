# Accessibility Modding Guide for Screen Reader Users

A guide for building game accessibility mods that let blind players play with a
screen reader (NVDA, JAWS, Narrator, VoiceOver, …). It is written from the work
on **Voice of the Old Republic** (KOTOR 1), but the first half is
game-agnostic — the principles apply to any title you want to make playable
without sight.

This guide is deliberately split:

- **Part 1 — Craft.** What to announce, how to format speech, how to design a
  keyboard, how to structure the code. Portable to any game.
- **Part 2 — Native-binary specifics.** The harder architecture that a
  compiled game like KOTOR forces on you (DLL injection, detour hooks, reading
  live engine memory) versus a managed game where you can patch IL directly.

> **Hard rule, read this first.** This project reverse-engineers a commercial
> game to find where to attach. We follow the line the KOTOR modding community
> has used publicly for years — the same one Lane Dibello's
> [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager)
> draws. The principle: you may publish **interoperability information**; you may
> not redistribute a **copy of the copyrighted work**.
>
> **Never commit or redistribute:** the game executable itself; the Ghidra
> project database (`.gzf`); the bulk decompiler exports (`.sarif`, `.xml`); the
> raw extracted header dump; or a **verbatim decompiled function body** pasted
> into a doc, issue, or PR.
>
> **Fine to publish** — Lane does, and so do we: function and global-pointer
> **addresses**, struct/member **offsets**, class layouts and method
> **signatures**, the original **byte patterns** a hook relocates, and prose that
> **describes** what a routine does. Our tracked docs and `hooks.toml` already
> carry these; that is deliberate, not a leak.
>
> See [Part 2 → Where the reverse-engineering lives](#where-the-reverse-engineering-lives)
> for how the repo enforces this split with `.gitignore`.

---

## Part 1 — Craft (portable to any game)

### Core accessibility goals

- Well-structured text output — no tables, no ASCII art, no graphics.
- Linear, one-thing-per-line presentation a screen reader can read top to bottom.
- Speech routed through a real screen-reader bridge (Tolk, Prism, or the
  native controller client), not a bare text-to-speech engine, so the user's
  own voice, rate, and punctuation settings apply.
- Full keyboard navigation to every interactive element. If a sighted player
  can reach it, a blind player must be able to reach it without a mouse.

### Screen reader communication principles

#### What to announce

- **Context changes** — screen transitions, mode changes, phase changes, area
  changes. These are the highest-value announcements; getting lost is the
  single worst failure mode.
- **The currently focused element** — its name, type, and state (selected,
  disabled, checked, quantity, …).
- **Game-state updates the player would otherwise read off the screen** —
  health, resources, turn/round, objective changes.
- **Available actions and their results** — what a key will do here, and what
  happened when it was pressed.
- **Error and confirmation states** — "way blocked", "not enough credits",
  "saved".

#### How to announce

- Output plain text, tuned for the ear, not the eye. Short first, detail on
  request.
- Speak the **most important token first** (the name, then the metadata):
  "Door, 3 o'clock, 5 metres" — not "5 metres away at roughly 90 degrees there
  is a door".
- Put detail in **navigable blocks** the user pulls up with arrow keys or a
  dedicated key, rather than reading a paragraph at every focus change.
- Always offer **repeat-last-announcement**. Speech is ephemeral; a missed cue
  with no way to replay it is a dead end.
- Announce automatically on important events, but **dedup and throttle** — a
  cue that fires ten times a second is noise, and noise trains the user to tune
  the mod out.

### Output formatting for screen readers

**Avoid**

- Tables — pipe (`|`) characters are read aloud one at a time.
- ASCII art or any spatial/graphical layout that only means something visually.
- Conveying information through position alone ("the top-left number").
- Multiple parallel columns.

**Prefer**

- Headings and bullet lists for structure.
- One item per line, consistent field order every time.
- Related fields grouped under a clear label.

**Example — instead of a table:**

```
Item Name
- Property: Value
- Property: Value

Another Item
- Property: Value
- Property: Value
```

### Keyboard navigation design

#### Reserved keys — do not override

- **Tab / Shift+Tab** — standard forward/back navigation.
- **Enter** — confirm / activate.
- **Escape** — cancel / back.
- The screen reader's own modifier and review keys (NVDA's Insert layer, the
  JAWS key). Never bind on top of them.

#### Recommended patterns

- **Single letters** for fast jumps to a zone or category (e.g. `M` for map).
- **Shift+letter** for the opponent's / alternate view of the same thing.
- **Arrow keys** for navigation *within* a focused area.
- **Ctrl / Alt variants** for "nearest / farthest", "beacon", "clear" — a
  consistent family of modifiers on the same base key.
- **F1 for help, F-keys for global actions**, Space for the primary action.

#### Context-aware keys

- The same physical key can do different things depending on state, but you
  must **announce the current context** so the user knows which meaning is
  live. A silent mode-switch is a trap.

#### Keyboard-layout independence (easy to forget)

If the game reads scancodes rather than characters, a key labelled `` ` `` on a
US keyboard is a different physical key on a German or French one. Decide early
whether your bindings are **character-based** or **position-based**, document
the mapping per layout, and make the "announce this object" / help keys reachable
on every layout you support. (KOTOR's world-interaction keys have a US form and
a German form — see the README's keyboard section for the pattern.)

### Speech routing

- **Default:** queue normal announcements so they don't stomp each other; dedup
  per channel so a repeated focus event doesn't repeat its speech.
- **Urgent:** some cues must interrupt and must survive the screen reader's
  "cancel speech on keypress" behaviour (e.g. NVDA cancels queued speech when
  you type). Route those through a path that speaks immediately.
- **Never silence a fallback.** If you don't have a good name for something,
  speak a generic placeholder ("Control 3", "row 2") — do **not** drop the
  announcement. Silence reads as "nothing is there", which is worse than an ugly
  label. (Placeholders should bypass dedup/throttle by design so they always
  come through.)

### Code architecture

#### Core principles

- **Modular** — separate input handling, state reading, text extraction, and
  announcement. Each should be replaceable without touching the others.
- **Maintainable** — one clear pattern per job, followed everywhere. A
  contributor should be able to guess where a thing lives.
- **Efficient** — you are running inside someone else's frame loop. Cache
  lookups, avoid per-frame allocations, and never block the game thread on I/O,
  speech, or network.

#### Utility layers to build once and reuse

- **Text extractor** — pull readable text out of a UI element.
- **Element activator** — trigger a control the way a click would.
- **Element/type detector** — classify what a thing is (button, list item,
  the game's domain objects).
- **Announcement manager** — queue, dedup, throttle, repeat-last.
- **Input dispatcher** — one place that owns key handling and context.
- **String table** — one place that owns every user-facing word (see
  [Centralizing user-facing strings](#centralizing-user-facing-strings)).

Always call the utility instead of re-implementing it inline. Duplicated
extraction/activation logic is the most common way these projects rot.

### Centralizing user-facing strings

Every word the user *hears* goes through a single string table keyed by a stable
ID; handler code calls `Get(id)` and never contains a literal. This buys you
localization for free and lets you reword a cue in one place. **Logs stay in the
developer's language** (English here) — only spoken output is localized. See
[docs/CONTRIBUTING_TRANSLATIONS.md](docs/CONTRIBUTING_TRANSLATIONS.md) for how
this project's table is laid out and how to add a language.

### Testing with a screen reader

- Test with **real screen-reader software**, not just a TTS log line. The
  timing, interruption, and punctuation behaviour only shows up for real.
- **Play with the monitor off.** If you can't do the task by ear, neither can
  the user.
- Verify every interactive element is *reachable*, not just readable.
- Get feedback from blind users; they will find the dead ends you can't.
- A change that compiles cleanly but produces no audible result is **not done**.

### Common pitfalls

- Announcing too much at once — burying the one thing that mattered.
- Not announcing a state change the player needed (silent mode switch, silent
  failure).
- Inconsistent bindings across screens — the same key doing unrelated things
  with no announcement.
- Overriding keys the screen reader or the game already own.
- Assuming visual context ("obviously the selected one") that was never spoken.
- Not handling rapid repeated keypresses — debounce or collapse them.

---

## Part 2 — Native-binary specifics (KOTOR and games like it)

A managed game (Unity/IL2CPP with a mod loader, .NET, Java) lets you patch the
game's own methods and read its objects through reflection. A **compiled C++
game from 2003 gives you none of that.** There is no metadata, no reflection, no
mod loader — just a running process whose functions and structures you have to
find yourself. This is the part of the work that is genuinely harder than the
arena-style managed mod, and it is where the "no decompiled code in public"
rule lives.

### The injected-DLL model

The mod is a 32-bit DLL loaded into the game process at startup (via a proxy
DLL the game auto-loads). Once inside, it can:

- **Hook** engine functions — redirect a function through our code so we run at
  a chosen moment.
- **Read** engine memory — walk the game's live objects to observe state.
- **Call** engine functions — invoke the game's own routines (e.g. to activate
  an action) instead of simulating input.

`DllMain` runs under the Windows loader lock, so **anything that loads a DLL,
opens a file, touches COM, or starts a thread must be deferred** to a "first
hook fires" path, not done at load time. Getting this wrong produces
hard-to-diagnose startup hangs.

### Hook versus poll

A recurring design decision:

- **Hook** when you need to react to *control flow* — "a dialog node opened",
  "a screen was shown", "a combat round resolved". You want to run exactly when
  the engine does a specific thing.
- **Poll** when you need to observe *state* — "what is the player's health this
  frame", "what is the focused list row". Reading once per frame from a tick
  callback is simpler and safer than hooking every writer of that state.

Default to polling for observation; reserve hooks for the moments you genuinely
need to intercept. A hook is a permanent detour into the middle of someone
else's function — it is the more fragile of the two.

### Hook design principles

_(General principles only. The addresses, byte patterns, and cut points come
from the internal RE material under `docs/llm-docs/re/`; you may cite them, just
don't reproduce a decompiled function body. See
[Where the reverse-engineering lives](#where-the-reverse-engineering-lives).)_

- Prefer hooking **mid-function with register-sourced arguments** over
  reconstructing a full stack frame, when the framework supports it.
- Land every new hook with a **logging-only handler first.** Confirm it fires
  when — and only when — you expect, *before* attaching behaviour.
- Log the trigger **unconditionally for the first few sessions.** Full fidelity
  beats a small log file when you're establishing that a hook is sound.
- One address per hook in the hook table; split per-game-version variants out
  only if the versions actually diverge.

**The KOTOR workflow, concretely:**

- The mod runs on **Lane Dibello's Kotor-Patch-Manager** (vendored under
  `third_party/`). It installs detours at load time from a declarative table —
  we never hand-write trampolines.
- Hooks are declared in **`patches/Accessibility/hooks.toml`** (currently ~72
  of them). Each entry is an `address`, a `type` (`"detour"`), a `function`
  name that maps to an exported handler, the `original_bytes` the framework
  relocates, and one `[[hooks.parameters]]` block per argument giving a
  register `source` and `type`.
- Handlers are exported through **`exports.def`** and implemented in the
  matching `.cpp` (by convention the `engine_*` file for the subsystem).
- **Source arguments from registers** (`"ebx"`, `"edi"`, `"esi"`, `"eax"`),
  not `"esp+X"` — the framework's stack-param path has a known LEA-vs-MOV bug
  that hands you an address where you wanted a value.
- Find the cut point and its bytes with the **headless Ghidra scripts** in
  `tools/ghidra-scripts/` (`Decompile.java` for a C-like view of a function,
  `DumpBytes.java` for the exact bytes to paste into `original_bytes`).
- **Land every hook with a logging-only handler first**, logging
  unconditionally via `acclog::Write` (see `log.cpp`) for the first few
  sessions. Confirm it fires when — and only when — you expect *before* adding
  behaviour. Don't rate-limit the diagnostic log; full fidelity beats file size.

The mechanical details and the reverse-engineering recipe live in
[CONTRIBUTING.md](CONTRIBUTING.md) ("Hooks" and "Reverse-engineering workflow").
Read `hooks.toml` alongside them — the comments on each hook are the best worked
examples in the repo.

### Engine reads

_(General principles only.)_

- **Verify a structure offset against the live binary before trusting it.**
  Reverse-engineered headers are mostly right but occasionally lag reality; an
  offset that reads plausible garbage is worse than a crash because it fails
  silently.
- Read the **narrowest type** the field actually is. A one-byte enum read as a
  four-byte int grabs three neighbouring bytes of garbage that then fail every
  comparison.
- Beware **client/server or dual-representation splits** — the same logical
  object may exist twice with different layouts; know which one you're holding.

**The KOTOR read-side conventions:**

- **Typed accessors live in the `engine_*` files.** Object, area, and player
  reads are in `engine_reads.cpp`, `engine_area.cpp`, and `engine_player.cpp`;
  GUI control reads are in `engine_panels.cpp`. Add a new read next to its
  peers, behind a named accessor, rather than dereferencing offsets at the call
  site. Handlers should read *through* a function, never poke raw memory inline.
- **Down-cast GUI controls via the vtable.** A base `CSWGuiControl*` is turned
  into the concrete type with the vtable-index helpers in `engine_panels.cpp`
  (`AsButton`, `AsLabel`, …). This is how you get at a control's text without
  guessing its layout. Use the helper; don't reimplement the cast.
- **Mind the client/server split.** The engine keeps a server-side truth/AI
  representation (`CSWS*`) and a client-side UI representation (`CSWC*`) of the
  same logical object, in one process, with *different layouts*. Reading the
  wrong side gives plausible garbage. Know which side your data is on — and note
  that object handles are namespaced per side, so AI primitives need a
  server id even if you started from a client one.
- **Read the narrowest type the field actually is.** The object-kind field, for
  example, is a one-byte enum; a four-byte read grabs three neighbouring bytes
  of garbage that then fail every comparison.
- **Verify an offset against the live binary before trusting it.** The
  reverse-engineered headers are mostly right but occasionally lag reality;
  cross-check against the internal RE material (below) or a decompile.

Several of these are written up in the memory index (`project_client_server_architecture`,
`project_gameobject_kind_is_one_byte`, `project_object_handle_namespaces`) — the
canonical place for "why this read is shaped this way".

### Where the reverse-engineering lives

The reverse-engineering material splits into two tiers, and the repo enforces
the split with `.gitignore` so the wrong tier can't be committed by accident.

**Internal-only — never committed.** These live under `docs/llm-docs/re/`, which
`.gitignore` excludes:

- the Ghidra project database (`.gzf`),
- the bulk decompiler exports (`.sarif`, `.xml`),
- the raw extracted header dump (`swkotor.exe.h`),
- the system-layout PDF.

Because they're untracked, GitHub Pages never serves them either — Pages only
builds tracked files.

**Tracked and public.** The narrative RE notes and per-file summaries under
`docs/llm-docs/` (the `*.md` files and `code-index/`). These carry addresses,
offsets, signatures, and behavioural descriptions — the same class of
interoperability information Lane publishes in his committed address databases
and architecture docs.

**The rule for contributors:**

- You may cite **addresses, offsets, signatures, and byte patterns** in docs,
  issues, PRs, `hooks.toml`, and commit messages. That is how the project already
  works, and it matches Lane's public practice.
- You may **describe** what a game routine does. You may **not paste a verbatim
  decompiled function body** into a tracked doc, an issue, or a PR — summarize it
  in prose, and if you need to point at the original, reference the untracked
  source you verified it against (a query result, a session log).
- Never add the **game binary**, the **`.gzf`**, or the **bulk exports** to the
  repo. `.gitignore` already blocks the `re/` directory; keep it that way.

(One ergonomic convention, separate from the legal line: in **chat answers** the
project style is to name things by function/field name rather than by address,
because addresses bloat prose without informing the reader. In code, docs, and
`hooks.toml`, addresses are encouraged. See the repo `CLAUDE.md`.)

### Project-specific sections to fill in

The general craft above is stable. The following are KOTOR-specific and are
yours to write as the design settles — leave them as stubs until then:

#### Build and inner loop

The whole cycle is driven by **`kdev`**, the project's dev CLI. The one command
you'll use most is:

```
kdev dev        # clean → build → apply → launch, with the mod injected
```

Narrower commands: `kdev build` (compile only, incremental), `kdev apply`
(install the latest build into the game), `kdev launch` (start the game),
`kdev logs --follow` (tail the patch log), `kdev kill`, `kdev status`. Patch
logs land in `<install>/logs/patch-<utc>.log`. Full setup — the compiler
toolchain, the Steam path, the patch-manager release — is in
[CONTRIBUTING.md](CONTRIBUTING.md) under "Dev setup" and "The inner loop".

#### Module map — which file owns which surface

The file-name prefix tells you the role. Add a new file under the right prefix.

- **`core_*`** — lifecycle: DLL attach + deferred init (`core_dllmain`),
  per-frame tick dispatch (`core_tick`), settings (`core_settings`).
- **`engine_*`** (~33) — the read-side engine bindings. Area/objects, player,
  panels, input, keymap, options, nav-graph, compass, radial, action bar,
  sub-screens, level-up, script vars. This is the layer everything else reads
  through.
- **`menus_*`** (~49) — per-screen GUI narration. One file per game screen:
  character creation (`menus_chargen_*`), character sheet, equipment, journal,
  galaxy map, store, key-mapping, mod settings, Pazaak deck builder, level-up
  powers, abilities, credits, plus the shared machinery (`menus.cpp`,
  `menus_listbox`, `menus_editbox`, `menus_chain`, `menus_extract`).
- **`audio_*`** (~11) — cue playback and the 3D audio bus, footstep
  suppression, positional loops, pitch.
- **`guidance_*`** (~10) — movement help: pathfinding, autowalk, beacon,
  approach, spoken route descriptions.
- **`combat_*`** (~12) — combat entry/exit, target queries, the action queue,
  special-ability watch, result strings.
- **`cycle_*`**, **`filter_objects`**, **`discovery`**, **`narrated_target`** —
  the discovered-object cycle (input + per-map state) and what feeds it.
- **`map_*`** — the in-game map cursor and user-placed markers.
- **`camera_*`**, **`spatial_*`**, **`wall_*`** — camera orientation and the
  continuous wall-cue / room-shape layer (`wall_topology`, `spatial_wall_surfaces`).
- **Minigames** — `pazaak`, `turret_game`, `swoop_race` + `swoop_spatial_audio`,
  `minigame_aim`, `floor_puzzle`.
- **Feature one-offs** — `view_mode`, `unified_action_menu`, `interact_hotkey`,
  `examine_view`, `peek_description`, `help`, `hotkeys`, `input_pipeline`,
  `msg_router`, `party_*`, `stealth_watch`, `trap_watch`, `transitions`,
  `tutorial_*`, `intro_skip`, `dialog_speech`, plus area-specific fixes
  (`endar_softlock`, `spectator_scene`, `locked_recall`, `save_crash_guard`).
- **`probe_*`** / **`diag_*`** — diagnostic instruments, not shipped behaviour.
  Handy when investigating; don't build features on them.
- **`strings*`** — the i18n tables (see the translation guide). **`prism`** —
  the speech backend. **`update_checker`** — the auto-updater.

#### Navigation systems

KOTOR is a 3D RPG, so most of the work is keeping the player oriented. The
[README](README.md) describes these for players; here is the contributor-facing
"how it's built" view. Each layer narrates itself as it's used.

- **Target cycling (Q / E)** — the engine's own object cycle; the mod reads the
  new target and speaks it. Lives across `engine_*` reads + `narrated_target`.
- **Discovered-object cycle (`,` / `.`)** — a mod-owned, in-save per-map index
  of objects the player has found, grouped by category. Built from
  `cycle_state` + `filter_objects` + `discovery`, driven by `cycle_input`. The
  narrated-target slot is shared with the world cycle so Enter/autowalk/beacon
  all act on "the last thing announced".
- **Unified action menu (Shift+Enter)** — one menu that replaced the game's
  separate radial, target, and personal menus. **Hard rule:** it is populated
  *only* from the input pipeline, never re-populated from a tick/poll. See
  `unified_action_menu` and the memory entry `project_unified_action_menu_design`.
- **Map (M)** — `map_ui_cursor` makes the in-game map navigable; `map_user_markers`
  handles Shift+N pins. Map pins fold into the same `,` / `.` vocabulary.
- **Wall cues + room-shape** — a continuous positional-audio layer off the
  nearest walls (`spatial_wall_surfaces`), plus a spoken room descriptor
  (name / shape / exits) computed live from the walk-mesh (`wall_topology`,
  `engine_navgraph`).

#### Adding a new narrated screen

The repeatable pattern for wiring a new game screen (there are ~49 worked
examples under `menus_*`):

1. **Detect entry.** Screens surface through the panel-title path in `menus.cpp`
   (`AnnouncePanelTitle`). Identify the panel by its `.gui` name / control
   structure.
2. **Announce title-only on entry.** Speak the screen's title and the initially
   focused element — *not* a full enumeration of every control. Title-only on
   first sight is the house style (memory: `feedback_first_sight_title_only`).
3. **Read elements through typed accessors.** Use the `engine_panels.cpp`
   vtable down-casts (`AsButton`/`AsLabel`/…) to pull each control's text.
   Don't read offsets at the call site.
4. **Handle keys via the input pipeline, before the engine chain.** For keys the
   engine delivers to GUI panels (arrows, Enter), add a
   `TryHandleInput(activePanel, …)` that returns "consumed" *before* the generic
   chain handlers run — the model used by `menus_listbox`, `menus_editbox`, and
   `pazaak`. A Win32 poll (`GetAsyncKeyState`) is only safe for scancodes the
   engine drops before the manager hook (letters, `,` `.` `-`).
5. **Route all speech through the string table.** Add an `Id` to `strings.h` and
   an entry to every locale file; call `strings::Get(Id)`. No literals in the
   handler.
6. **Log first, behave second.** Land the detection with a logging-only handler,
   confirm it fires exactly on that screen, then add narration.

---

## See also

- [CONTRIBUTING.md](CONTRIBUTING.md) — dev setup, the inner loop, hook and
  speech conventions, the reverse-engineering workflow, and how to send a PR.
- [ARCHITECTURE.md](ARCHITECTURE.md) — the code's high-level shape.
- [docs/CONTRIBUTING_TRANSLATIONS.md](docs/CONTRIBUTING_TRANSLATIONS.md) —
  adding or improving a language.
- [docs/known-issues.md](docs/known-issues.md) — the current backlog.
