# Changelog

All notable changes to the Voice of the Old Republic mod.

Versioned releases below. The release script (`installer/release.ps1`) reads the
topmost `## vX.Y.Z` heading to determine the version it ships, and uses the
bullets under that heading as the GitHub release body. When preparing a release,
rename `## Unreleased` to the new version and add the relevant bullets — group
them under short topic headings (`Examine view:`, `Map:`, `Combat log:`,
`Installer:`, `Bug fixes:`) the way `arena/docs/CHANGELOG.md` does.

## v0.2

_In development. Add v0.2 changes as bullets under topic headings
(`Examine view:`, `Map:`, `Combat log:`, `Installer:`, `Bug fixes:`)
before running `installer/release.ps1` to cut the release._

**Installer:**

- Auto-launch at the end of install now honours the user's chosen install path. The previous behaviour fired `steam://run/32370` unconditionally, which made Steam launch whatever copy Steam had registered for KOTOR — fine for the default Steam install, but wrong for GoG copies, CD re-packs, manually-relocated Steam folders, or any user-specified custom path (Steam would silently launch the wrong copy or no copy at all, so the user's freshly-patched install never ran). The installer now checks whether the chosen path matches Steam's registered install for App ID 32370; if it does, the steam:// route still runs (preserves Steam overlay + cloud saves + a non-elevated launch); otherwise it launches `swkotor.exe` directly from the configured path.
- Mod-selection screen now shows only the K1CP toggle; the restored-cut-content and companion/swoop-upgrade toggles were no-ops (no installer wired up yet) and have been commented out until those installers land.
- "Collect logs" bundle no longer balloons to hundreds of megabytes. Windows Error Reporting was registered to capture full swkotor.exe process dumps, which routinely came out at 500 MB+ because the engine maps a lot of texture, audio, and BIF data we never read during triage. The installer now configures WER with a targeted custom flag set (data segments, indirectly-referenced heap pages, per-thread state, and the unpacked code segment), which preserves everything our `kdev analyze-dump` workflow relies on — stack walks, registers, heap pages pointed at from the stack (so freed-slot forensics like the recent save-popup use-after-free still work), and runtime-decrypted instructions — while dropping the asset memory that made up most of the size. Typical dump size is now ~15-50 MB instead of ~500 MB+. Note: the new flag set takes effect once you re-run the 0.2 installer or click "Collect logs" once (both refresh the WER registry entry). Crash dump files already sitting in `%LOCALAPPDATA%\CrashDumps` from earlier crashes are still in the old large format until WER captures a new crash with the updated flags.

**Dialog:**

- Human-speaker subtitles are no longer read aloud by default. KOTOR 1 ships full English / German voiceover for every human-voiced NPC, so the screen-reader was repeating the same line on top of the voice actor in a different cadence — a constant clash that made conversations harder to follow, not easier. Non-human speakers (Twi'lek, Ithorian, Rodian, Selkath, Wookiee, droids, etc.) continue to read in full, because their voiceover is the alien language and the subtitle is the only comprehensible channel. Classification is by speaker species via `appearance.2da`; the conservative call (Twi'lek, Cathar, Mandalorian, etc. all keep TTS on) means alien lines are never accidentally suppressed. If you want the old behavior — TTS reads every subtitle including humans — enable "Untertitel menschlicher Sprecher vorlesen" / "Read human-speaker subtitles" in Mod-Einstellungen.

**Bug fixes:**

- Dialog text no longer read twice by parallel speech paths. The dedicated dialog speech module and the generic panel content monitor were both announcing the NPC line each tick — fine in v0.1 because they used the same backend, but as soon as the new human-suppression toggle landed only one of them respected it, so suppressed lines still slipped through the monitor and reached the screen-reader unchanged. Dialog and bark panels are now owned by the dialog module alone.
- Tooltip and message-box / tutorial text no longer truncated at ~256/1024 characters; long descriptions and atmospheric text now read in full.
- Game no longer crashes ~8 seconds after the main menu loads on systems without a connected mouse. Latent BioWare engine bug: when DirectInput mouse init fails, the engine releases its DirectInput interface but reports success, causing the next per-frame mouse poll to dereference NULL. Guarded at `CExoRawInputInternal::InitializeDirectInputMouse`; keyboard input is unaffected.
- Main menu reliably accepts keyboard input on first focus after launch. The earlier guard for the no-mouse crash, installed via the standard hook wrapper at the DirectInput-mouse init function entry, was interfering with the engine's foreground-cooperative-level handshake on cold start and forcing an alt-tab to wake the menu. The guard now installs inline (no wrapper) and only after the engine's first successful mouse init, so the cold-start path runs untouched and the second-call NULL-deref protection is still in place.
- Game no longer crashes a few seconds after saving. The engine frees the SaveLoad panel synchronously when the save commits, but our tab-cluster detector was still holding the freed pointer from the previous focus event and crashed when the heap allocator reused those bytes for combat-log strings. Now panels[]-validated before deref, matching the existing pattern for chain/tab panel guards.
- Shift+S no longer crashes the game. The key was wired to an experimental "selected character stat block" readout that called engine stat accessors with unvalidated addresses, occasionally smashing the stack canary and triggering an uncatchable fast-fail. The feature was never reliable in practice (the bare H self-status hotkey covers the same ground), so it has been removed entirely.
- Options-screen tabs (Gameplay / Feedback / Auto-Pause / Grafik / Sound) now activate the entry you actually focused. Previously Enter on a tab — and arrow stepping across tabs — landed one row above the intended one, so picking "Sound" opened "Grafik", picking "Feedback" opened "Gameplay", and so on. Caused by a race between the chain rebuild and the tabbed-panel detection: the engine's hit-test shift compensation never had a chance to populate before the click fired. The compensation is now derived on-demand at warp/click time instead of being cached at rebuild time.
- Shift+L (open level-up screen) no longer stacks panel copies on key-repeat. Pressing Shift+L while the level-up screen was already open allocated a fresh panel on every press, leaving the user trapped: the screen was only partially drawn, Esc only popped one of the many duplicates at a time, and the engine's Alt+F4 quit confirmation could never reach the foreground. Subsequent presses while the level-up screen is open now announce "Stufenaufstieg bereits offen" and do nothing.
- Taris Sith-base "Lights Out" wall-switch state announcements (on/off) now localise for French, Italian, and Spanish — previously fell through to English because the per-switch label table only carried German + English columns instead of routing through the shared strings system.

---

## v0.1

First public beta. Playable from new-game / chargen through the entire main quest
arc up to the Leviathan boarding-turret minigame; the turret sequence itself is
not yet accessible and is the documented end of the beta window.

**Highlights:**

- Full character creation: gender, portrait, class, ability scores, skills, feats, name entry
- Spatial wall-distance audio cues + room shape / exit announcements (Pillars 1 & 2)
- Q / E target cycling, `,` / `.` parallel cycle covering doors, transitions, waypoints, map pins
- Autowalk (`Shift+-`) and audio-beacon navigation (`Ctrl+-`) over the engine's nav graph
- Examine, level-up, equip, inventory, party-pick, character-sheet, journal, store, container, workbench, save / load — all keyboard-navigable with full speech
- In-world combat narration (attacker → action → target, hit / miss / damage), action-queue submenu
- In-game map with cycleable pins, fog-of-war respect, user-placed `Shift+N` markers
- View mode (`B`) for "look without walking" / autowalk to distant landmarks
- Compass + 90° turn-snap (`N`) for orientation
- Localisation: English, German, French, Italian, Spanish

**Supported game versions:**

- Steam KOTOR 1, v1.0.3 (the current Steam build)
- GoG KOTOR 1, v1.0.3 (and byte-identical CD re-packs)
- Languages: English, German, French, Italian, Spanish

Not supported in v0.1: Aspyr mobile / macOS ports, UniWS-modified executables, and any
exe whose SHA-256 doesn't match the two recognised builds. If the installer reports a
version mismatch, file an issue with the displayed hash so we can add your variant.

**Known issues in v0.1** (won't be fixed before tagging — please don't file duplicates):

- Menus lag noticeably on first open in a session; subsequent opens are smooth
- Character occasionally spins erratically in-world; no reliable repro yet
- Q / E target cycle sometimes announces "no target" during a cycle step
- Map-hint filter double-announces some items per cycle step
- Turret minigame (Leviathan, mid-game space encounters) — no accessibility
- Pazaak card game — no accessibility
- Star map / galaxy travel screen — limited narration
- HP bars for player and party not yet on a read-on-demand hotkey
- Open-space room narration is weaker than corridor / junction narration

See `docs/known-issues.md` for the live tracker.

**Installation notes:**

- Run `KotorAccessibilityInstaller.exe` as administrator. The first run will trip
  Windows SmartScreen ("Unknown publisher") — click "More info → Run anyway".
  The installer is not code-signed yet.
- Back up your save folder (`%USERPROFILE%\Documents\Swkotor\saves\`) before
  installing if you have an in-progress vanilla playthrough.
- The bundled K1 Community Patch makes deep edits to dialog, scripts, and 2DA
  tables. Saves made on a K1CP install are NOT guaranteed to load cleanly on a
  vanilla install.

**Uninstall:**

- Run the installer again and choose Uninstall, or use Add / Remove Programs.
- Uninstall removes only this mod's files (KotorPatcher runtime, our `patches/`
  folder, the registry entry). K1CP and any other optional mods you chose at
  install time are left in place.
- To return to fully vanilla KOTOR, after uninstalling this mod use Steam's
  "Verify integrity of game files" or reinstall from GoG. That step will also
  remove K1CP — there is no in-installer "uninstall K1CP" path yet.

---

For session-by-session retrospectives and the historical investigation record,
see `archiev/`.
