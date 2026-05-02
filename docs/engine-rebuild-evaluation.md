# Engine Rebuild Evaluation — Pivot or Stay?

Evaluation date: 2026-04-30
Question: should the accessibility mod target one of the open-source KOTOR engine rebuilds instead of the original Odyssey binary?

## TL;DR — Recommendation

**Stay on the original engine for the player-facing mod. Track reone and KotOR.js as long-horizon options.**

None of the four rebuilds can take a screen-reader user through KOTOR 1 from new game to ending today. The closest thing to an end-to-end playthrough is still the original `swkotor.exe`, which is exactly where blind players already own and run the game. A pivot would trade a working substrate for a partially-working one and freeze our delivery for an indefinite time horizon.

The investment so far (Ghidra DB, verified hook addresses, struct offsets, kdev pipeline, KPatchManager integration) is heavily original-engine specific, but most of the *design* knowledge — what to announce when, how to present a listbox to a screen reader, focus model, navigation grammar — transfers cleanly to any backend. So even if we eventually port to a rebuild, the original-engine work is not wasted, it is the spec.

There is a strong **secondary play** worth pursuing in parallel: open accessibility-hook PRs against reone (and possibly KotOR.js) so that whichever rebuild reaches feature parity first ships native screen-reader support. That's cheap to scope and high-leverage — we get to design the UI semantics once and have them land in a clean codebase, rather than always extracting them through detours.

---

## The Four Candidates

### reone — C++, mid-stage, actively developed

- **Repo:** `https://github.com/seedhartha/reone`
- **License:** GPL-3.0
- **Language:** C++ (96%), GLSL, CMake
- **Activity:** ~303 stars, last commits April 2025 (toolkit fixes), regular but not daily cadence
- **KOTOR 1 playability:** Endar Spire is the target for the 0.20 milestone. Per the project roadmap:
  - Done: module loading, movement, pathfinding, models/animations, walkmeshes, collision, resource management
  - Partial: items, triggers, conversations, combat, party, saves, equipment / inventory / character sheet UIs
  - Not started: Force powers, grenades, traps, encounters, stores, stealth, map exploration, journal, workbench
- **End-to-end playable?** No. 1.0 (= "completable with parity to vanilla") not reached.
- **Mod compatibility:** Asset-style mods ("most should work"). Mods that touch the executable or rely on under-studied formats won't. New animations not yet supported.
- **Accessibility-relevant qualities:** Modern C++ codebase, owns its own GUI rendering, has a native widget hierarchy. Adding screen-reader output is a question of "where do I emit a focus event" rather than "where is this address in a stripped binary." Trivially easier to instrument than the original engine.

### KotOR.js — TypeScript / Electron, very actively developed

- **Repo:** `https://github.com/KobaltBlu/KotOR.js`
- **License:** GPL-3.0
- **Language:** TypeScript (97%)
- **Activity:** ~133 stars, **commits within the last 48 hours** as of this writing — by far the most active rebuild right now. Multiple commits per day on game state, dialog, modules, audio.
- **KOTOR 1 playability:** README still says "early stages." Demo footage shows Taris and Dantooine traversal; combat animations exist; full quest / save / load completeness is not documented or asserted.
- **Mod compatibility:** Not described in the README. Has a parallel modding suite ("KotOR Forge") in the repo, but Override-folder semantics are not called out.
- **Accessibility-relevant qualities:** This is the dark-horse candidate for accessibility work. The runtime is Chromium under Electron, which means the UI is HTML/CSS/JS — the entire screen-reader stack (ARIA roles, live regions, focus management, NVDA/JAWS browse mode) just works out of the box if the UI is built with semantic HTML. Of the four candidates, KotOR.js has the lowest theoretical effort to a screen-reader-first port — *if* its UI is built with DOM elements rather than canvas-only WebGL. That needs verification. If the UI is rendered into a WebGL canvas like the 3D world, the advantage evaporates.
- **Catch:** Activity cadence is great, but "active" ≠ "complete." Same end-to-end question mark as reone.

### xoreos — C++, actively maintained but stuck on KOTOR

- **Repo:** `https://github.com/xoreos/xoreos`
- **License:** GPL-3.0
- **Language:** C++ (92%)
- **Activity:** Commits as recent as April 2026, but content of recent work is build-system maintenance — Boost 1.89 compatibility, GLEW updates, copyright bumps, std-library modernization. **No KOTOR1 gameplay progress in the visible recent history.**
- **KOTOR 1 playability:** Stuck at "tutorial partially playable" since the 0.0.6 release in **August 2020**. Self-described as "pre-pre-alpha :P". Endar Spire tutorial: collect belongings, gear up, follow Trask through two doors, mock combat. Full game: not playable.
- **Mod compatibility:** Engine reads file formats but actual mod-loading semantics aren't a stated goal at this stage.
- **Verdict:** Not a viable target for an accessibility mod that needs to ship to users. Useful as cross-reference source code only — its file-format reader is mature and the implementation choices are valuable when reverse-engineering the original.

### Northern Lights — Unity / C#, level-editor focused

- **Repo:** `https://github.com/lachjames/NorthernLights`
- **License:** AGPL-3.0
- **Language:** C# (Unity), with NWScript
- **Activity:** ~29 stars, 60 commits total, no releases. README states "engine reimplementation is not ready for prime-time yet."
- **KOTOR 1 playability:** No. Project is centered on the KotOR Level Editor (KLE).
- **Verdict:** Not a viable accessibility target. AGPL is also a redistribution friction point if we were to build a derivative.

(There's also `KotOR-Unity` by reubenduncan / rwc4301 — also Unity-based, also not playable end-to-end. Same bucket.)

---

## What "Should We Pivot?" Actually Means

It helps to separate the question into three independent decisions:

1. **Where does the player run the game?** Today: the original `swkotor.exe`. Pivoting means waiting on a rebuild to hit "completable end-to-end" before any blind player can finish the game. None are there yet.
2. **Where does our patch / hook code live?** Today: a `.kpatch` injected into the original. Pivoting would mean writing C++ extensions for reone, or TS modules for KotOR.js, or Unity components for Northern Lights.
3. **Where does the design knowledge live?** Already in our docs (`menu-nav-design.md`, the listbox interaction model memos, focus-event semantics). Backend-agnostic.

Decision 3 is portable for free. Decision 1 is decided by the rebuilds, not by us. Decision 2 is the only one we actually choose, and it depends on what we want to ship and when.

## Advantages of Switching to a Rebuild

- **No reverse engineering.** Source code is right there. Adding focus events to a button class is a 5-line PR, not a Ghidra session.
- **No detour fragility.** No more LEA-vs-MOV upstream bugs, no patch versioning, no Steam/GoG byte alignment headaches.
- **Native a11y stack on KotOR.js.** If the DOM is real, NVDA/JAWS just work — no Tolk, no synthesizer plumbing.
- **First-class status for the contribution.** A "screen reader support" PR upstream becomes a feature, not a third-party patch users have to install.
- **Cross-platform for free.** Original is Windows-only; all four rebuilds target Linux at minimum.
- **No legal/licensing gray area.** Modifying running binaries with detours has always lived in mod-tolerance limbo. Patching open-source code does not.

## Disadvantages / Risks of Switching

- **The game has to be playable end-to-end first, and none of them are.** This is the dominant risk. A blind player cannot start KOTOR on reone today, finish it, and credit our mod for it. The mod has no audience until the host engine ships.
- **Save game compatibility.** Players with original saves probably can't bring them over. New game on a rebuild = mandatory new playthrough, which is a tall ask.
- **Mod ecosystem collision.** Players who use restoration / textures / TSLPatcher mods can't necessarily run them on a rebuild. Forcing an a11y user to choose between accessibility and the rest of the mod scene is bad.
- **Audio & cinematic gaps.** Rebuilds historically have rough edges in lipsync, conversation cameras, video playback (Northern Lights uses MP4 conversions). Subtitle / VO timing is *load-bearing* for a screen-reader player — if the rebuild's dialog system isn't right, our mod inherits the bug.
- **Project lifecycle risk.** Open-source rebuilds stall. xoreos is the cautionary tale: actively committed, not actively progressing on the goal that matters. We could pick a winner and watch it freeze.
- **License (AGPL).** Northern Lights specifically would obligate us to AGPL anything we ship that integrates with it. Probably fine for an accessibility mod, but worth flagging.
- **Sunk cost on hooks is real but partial.** Our verified `SetActiveControl` hook, GUI struct offsets, and KPatchManager integration are original-binary specific and would not port. The `kdev` tool, the Ghidra-based investigative habits, and the conceptual map of the GUI hierarchy do port.

## Edge Cases Worth Calling Out

- **The "screen reader announces the loading screen" problem.** On the original engine we have to detour into the loading-screen state machine. On reone or KotOR.js, the engine controls when "loading" begins and ends — trivially announce-able. This is one of the small wins that compounds across the design.
- **The "is this listbox empty or just scroll-mode" problem.** We solved it via the `selection_index = -1` heuristic on the original. In a rebuild, the listbox class has a clear `IsEmpty()` method. The fact that we already understand the *semantics* means we can trivially apply them to a rebuild — but the trivially-applicable-ness only kicks in if we ever do.
- **NVDA/JAWS automation surface.** On the original engine we depend on Tolk or direct synth APIs. On KotOR.js, the browser exposes `aria-live`, role hierarchies, focus model, etc. — a much wider, more standard surface. Worth experimenting with even purely as a research spike.
- **"Two engines diverge" risk.** If we maintain both an original-engine patch and a rebuild contribution, behavior can drift between them and bug reports become ambiguous ("does this happen on reone?"). Worth picking one as canonical.
- **Subtitles / VO routing.** On a rebuild we can route VO line text into the screen reader natively at the moment the line plays. On the original engine we are reverse-engineering the dialog state machine to do the same thing. The rebuild path is dramatically less work — *if* the dialog state machine on the rebuild is actually working at parity, which today it is not.

## Open Questions Before Any Pivot Decision

1. **KotOR.js UI architecture:** Is the in-game UI built with HTML/DOM elements (good for screen readers) or rendered to a WebGL canvas (no better than the original)? Worth a one-hour spike — clone the repo, run it, inspect the UI tree.
2. **reone playthrough state:** Can a reone build actually make it through the Endar Spire tutorial without crashing on the latest master? Roadmap says yes for 0.20; reality may differ.
3. **reone's module loader vs Override:** Do typical Override mods (e.g. K1CP — community patch) actually load on reone? If yes, the "lose the mod scene" disadvantage softens significantly.
4. **Save format compatibility:** Can any rebuild load a vanilla save? Even partially? This determines whether a player can take an existing playthrough into a rebuild.

## Recommended Path Forward

**Primary track (continue):** Original engine. Continue the menu-nav redesign work in `docs/menu-nav-design.md`, keep extending the patch DLL. This is the only path that yields a usable mod for blind players in the near term.

**Secondary track (start small):** Open a tracking doc for accessibility upstream targets. Pick reone as the first one — it's the most architecturally similar to the original (C++, native widget hierarchy, native engine loop) so design transfers most cleanly. Open one small PR — emit a "focus changed" event on the GUI panel class, with a debug logger consumer. That's it. We learn how the project receives external contribution, we have a foothold, and we ship something concrete.

**Watchlist:** Re-evaluate every 6 months. The trigger to seriously reconsider is *not* "rebuild reaches v1.0" — it's "a non-developer player reports completing KOTOR start to finish on a rebuild without resorting to console commands or save edits." Until that happens, the original engine remains the only game in town for actual users.

**Don't pivot now. Don't rule it out forever.**
