# Changelog

All notable changes to the Voice of the Old Republic mod.

Versioned releases below. The release script (`installer/release.ps1`) reads the
topmost `<h2>vX.Y.Z</h2>` heading to determine the version it ships (legacy
`## vX.Y.Z` is still recognised for older sections), and uses the bullets under
that heading as the GitHub release body. When preparing a release, rename the
`Unreleased` section to the new version and add the relevant bullets, grouped
under short topic subheadings (`<h3>Installer:</h3>`, `<h3>Bug fixes:</h3>`,
etc.). Subsection headings use HTML tags so GitHub renders them as proper
headings in the release body (markdown `**Installer:**` only renders as bold).
Each bullet should lead with the user-facing change ("X works now", "new
hotkey Y", "X no longer does Z"); keep technical detail short.

<h2>v0.1.2</h2>

<h3>Installer:</h3>

- Intro logo movies (BioWare / LucasArts / legal) are now skipped on launch by default. Eliminates 10-20 s of intro playback on cold start and avoids the engine bug where Alt+Tab during the intros restarts the queue. Toggleable at runtime under Mod-Einstellungen → "Intro-Movies überspringen"; change applies on next launch.
- Installer UI now available in French, Italian, and Spanish. Translations are AI-drafted (German remains the human-authored quality bar); flagged in `known-issues.md` for native-speaker refinement.
- Bundled `dinput8.dll` proxy refreshed to the latest loader build.

<h3>Startup:</h3>

- "Bitte warten" hint is now spoken if you press arrow / Enter / Space while the post-intro main menu is still loading. After 15 s of continued pressing, a second cue tells you about the Alt+F4 → cancel-dialog workaround for the known engine stall in the main-menu input pump.
- Main-menu title now reads as "Hauptmenü" instead of leaking whichever DLC-notice label the engine had focused first.

<h3>Action menus:</h3>

- Shift+Up / Shift+Down on the target-action menu (Shift+1..3) and the action radial now read feat and force-power descriptions in addition to items. Plain verb actions (Attack, Open Door, ...) fall through to "Keine Beschreibung verfügbar".
- Shift+Up / Shift+Down on the personal action bar (Aktionsmenü, Shift+4..7) now read the full item property description instead of three bytes of CP1252 garbage. The engine never populates the tooltip slot we were reading; resolver now goes through the descriptor's tagged `action_id`.
- Shift+Enter on objects whose radial has no extra options (already-open doors, NPCs you can only talk to, ...) now speaks "Keine Aktionen verfügbar für X. Enter zum Aktivieren." instead of the bare "Aktionsmenü, X" that left the user wondering what to do next.

<h3>Dialog:</h3>

- First NPC line in a conversation is no longer occasionally double-spoken. The generic first-sight title walk was speaking the dialog panel's first label child — which IS the NPC line — and slipped past the existing suppression. Dialog and bark panels are now skipped by the title walk.

<h3>Game state:</h3>

- Pressing Pause (default Space) in-world now speaks "Pause." when paused and "Pause aufgehoben." when resumed, so you hear the state change without watching the screen. Menu opens, popup closes, and our own audio-resync cleanup are suppressed so the cue doesn't fire on top of menu narration. Engine autopauses (combat, dialog, mine-sighted, etc.) use other pause sources that aren't mapped yet and stay silent for now. Support logs also gain one `Pause: fire ...` line per engine `SetPauseState` call with caller address + mask + on/off direction, so future pause-state regressions are traceable from a single log without rebuilds.

<h3>Bug fixes:</h3>

- F5 in-game auto-update no longer fails with "Download fehlgeschlagen" on every press. The patch DLL was looking for the pre-rename installer EXE in the GitHub release JSON, so the asset lookup always missed and the download bailed before it started. Existing 0.1.1 users will need to manually re-run the installer once to pick up this fix; their broken DLL can't fetch a working replacement via F5. `release.ps1` now preflight-asserts consumer-side filenames and version strings against what it's about to publish, so this drift class fails at release time instead of in users' hands.
- Shift+Up / Shift+Down inside the equipment-screen item picker now reads the focused item's description. The peek path was matching the originating slot button's control id and treating the slot as empty (the engine moves the equipped item out of the slot while the picker is open, so the cached handle reads as "no item"), so the read silently no-op'd before consulting the picker listbox. Slot path now gates on "picker not armed".

## v0.1.1

**Installer:**

- Auto-launch at the end of install now honours the user's chosen install path. The previous behaviour fired `steam://run/32370` unconditionally, which made Steam launch whatever copy Steam had registered for KOTOR — fine for the default Steam install, but wrong for GoG copies, CD re-packs, manually-relocated Steam folders, or any user-specified custom path (Steam would silently launch the wrong copy or no copy at all, so the user's freshly-patched install never ran). The installer now checks whether the chosen path matches Steam's registered install for App ID 32370; if it does, the steam:// route still runs (preserves Steam overlay + cloud saves + a non-elevated launch); otherwise it launches `swkotor.exe` directly from the configured path.
- Mod-selection screen now shows only the K1CP toggle; the restored-cut-content and companion/swoop-upgrade toggles were no-ops (no installer wired up yet) and have been commented out until those installers land.
- "Collect logs" bundle no longer balloons to hundreds of megabytes. Windows Error Reporting was registered to capture full swkotor.exe process dumps, which routinely came out at 500 MB+ because the engine maps a lot of texture, audio, and BIF data we never read during triage. The installer now configures WER with a targeted custom flag set (data segments, indirectly-referenced heap pages, per-thread state, and the unpacked code segment), which preserves everything our `kdev analyze-dump` workflow relies on — stack walks, registers, heap pages pointed at from the stack (so freed-slot forensics like the recent save-popup use-after-free still work), and runtime-decrypted instructions — while dropping the asset memory that made up most of the size. Typical dump size is now ~15-50 MB instead of ~500 MB+. Note: the new flag set takes effect once you re-run the 0.2 installer or click "Collect logs" once (both refresh the WER registry entry). Crash dump files already sitting in `%LOCALAPPDATA%\CrashDumps` from earlier crashes are still in the old large format until WER captures a new crash with the updated flags.

**Dialog:**

- Human-speaker subtitles are no longer read aloud by default. KOTOR 1 ships full English / German voiceover for every human-voiced NPC, so the screen-reader was repeating the same line on top of the voice actor in a different cadence — a constant clash that made conversations harder to follow, not easier. Non-human speakers (Twi'lek, Ithorian, Rodian, Selkath, Wookiee, droids, etc.) continue to read in full, because their voiceover is the alien language and the subtitle is the only comprehensible channel. Classification is by speaker species via `appearance.2da`; the conservative call (Twi'lek, Cathar, Mandalorian, etc. all keep TTS on) means alien lines are never accidentally suppressed. If you want the old behavior — TTS reads every subtitle including humans — enable "Untertitel menschlicher Sprecher vorlesen" / "Read human-speaker subtitles" in Mod-Einstellungen.

**Bug fixes:**

- Dialog text no longer read twice by parallel speech paths. The dedicated dialog speech module and the generic panel content monitor were both announcing the NPC line each tick — fine in v0.1 because they used the same backend, but as soon as the new human-suppression toggle landed only one of them respected it, so suppressed lines still slipped through the monitor and reached the screen-reader unchanged. Dialog and bark panels are now owned by the dialog module alone.
- Combat log and container loot panels no longer announce each row twice. The dedicated per-row navigation path (Up / Down moves through the list, focused row read once) was running in parallel with the same generic content monitor that hit the dialog bug above. Same fix shape: those two panels now have a single owner.

**Menus:**

- Several menus now read more cleanly on open. The Charakterblatt no longer dumps the full stat block, the skills and inventory screens no longer rattle off every row as one long line, and the equipment screen no longer reads its stat sidebar back at you. Each of those screens now announces only its name on open; arrow navigation reveals contents row by row, and existing self-status hotkeys still surface the same information on demand.
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
