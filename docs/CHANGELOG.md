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

<h2>v0.3.1</h2>

<h3>Navigation:</h3>

- Your party members no longer make the proximity sound cue as you walk around them. The previous release stopped companions from setting off the focus "person nearby" cue, but a second, continuous cue — the proximity beacon that pulses for nearby creatures — was still firing for them, so in an otherwise empty area you would hear a steady person signal from Carth or Mission trailing behind you. Companions are now left out of that beacon as well. Other creatures, including enemies, still pulse so you can hear them coming, and targeting a companion with Q / E still reads their name and status.
- Pressing Tab to hear who you are leading now reads your character's real name. While you were controlling your own character it read a leftover placeholder ("test") instead of the name you chose at character creation; your companions read correctly. It now reads your chosen name.
- Map hints and cycled objects that share a name are now numbered, so you can tell them apart and refer back to a specific one. Some map markers repeat the same label along a path — four "Nordpfad" hints on Dantooine, for example — and cycling with comma / period (or Q / E in the world) read the same name over and over with no way to distinguish them. Same-named entries now get a number: "Nordpfad 1", "Nordpfad 2", and so on. Fixed things — map hints, doors, footlockers, area transitions — are numbered by location from north to south, so the northernmost is 1. Because the number comes from the spot itself and not from how close you are, the same marker always carries the same number: on every visit, in every save, and for every player, no matter which direction you arrive from. Creatures can move, so they instead keep a single fixed number across the comma / period cycle, Q / E and the combat log.

<h2>v0.3.0</h2>

<h3>Dialogue:</h3>

- More human characters now stay quiet under their own voice. With the human-speaker subtitle filter on (the default), the mod skips reading a subtitle out loud when the character is already speaking it in a voice you can understand, so the screen reader no longer talks over the recorded line. Two gaps in that filter are now closed. First, several voiced human characters — Vrook Lamar on Dantooine among them — were wrongly tagged as alien internally, so their subtitles were still being read over their voice; they're now recognised as human and stay silent. Second, conversations you only overhear — where two characters talk to each other and you aren't part of the exchange, like the Taris cantina scene — were always read aloud, because the mod could only identify the speaker when you were the one being spoken to; it now identifies the speaker on every line, so overheard human dialogue is suppressed the same way. As before, this only affects voiced speech you can already understand: most alien and droid speech, and anything unvoiced, is still read, and you can turn the whole filter off under Mod-Einstellungen to have every subtitle read regardless.
- Non-human party members who speak Basic now have their subtitles suppressed under their own voice too. Mission (a Twi'lek) and Juhani (a Cathar) speak Basic with full voice acting, but the subtitle filter only recognised human characters, so the screen reader read their lines aloud over their recorded voices. They're now treated like human speakers and stay quiet. Genuinely alien speech you can't follow otherwise — Zaalbar's Shyriiwook, background Twi'lek chatter — is still read as before.
- Choosing a dialogue reply with a number key no longer also fires a combat action. Pressing 1–7 to pick a reply was additionally triggering the matching action-bar or target hotkey — attacking, using an item, or speaking a phantom "used" cue — because the mod kept refreshing the combat action bar on those keys even mid-conversation. Number keys now only select the reply while a dialogue is open.
- Dialogue no longer announces how many replies are available when a conversation node opens. The screen reader read out a count like "3 replies available" on top of reading each option as you arrow through them; since the options are numbered anyway, the count was redundant and is removed.

<h3>Navigation:</h3>

- Your own party members no longer set off the "person nearby" cue and name announcement as they wander around you. While exploring, the passive narration kept locking onto your companions — Carth, Mission, whoever is travelling with you — as they walked past or ahead of you, replaying the person cue and reading their name over and over. They're now skipped while you move around. You can still target a companion on purpose with Q / E, which reads their name and status as before (just without the person cue), so checking on a companion or talking to one still works.
- The navigation beacon can now be heard when the next waypoint is far away. The beacon's directional pulse is tuned for the short range the game's other cues use, so once a waypoint was more than about twenty metres off its pulse dropped below hearing and you lost the direction to follow. Distant waypoints now stay audible, with the pulse still pointing the right way and growing louder as you close in.

<h3>Combat:</h3>

- Combat now tells you when one of your own party members is hit. You hear a short report — who was struck, how much damage, and who hit them (with a "critical" note when it applies). These hits were being dropped silently until now: internally the mod could never tell which creatures were actually in your party, so it never recognised a companion as the one taking the blow. That detection is fixed, so hits landing on your companions are read out.

<h3>Minigames:</h3>

- The Ebon Hawk turret-defense sequences now play a sound cue to help you aim at incoming fighters. The cue tracks your locked target — pulsing when your aim is off and pointing the way to swing, going solid when you're on target so you know when to fire. This is an early, rough pass: landing a hit is still largely down to luck and the cue needs sharpening in a future update. An auto-aim easy-mode option is wired into Mod-Einstellungen but is currently broken; it'll be fixed alongside the aiming improvements.

<h3>UI:</h3>

- The small "OK" notification popups — a new journal entry, credits or experience gained, light- or dark-side points, items received or lost — now read their message. The screen reader reads the notification aloud as the popup appears (for example "Neuer Tagebucheintrag" when a quest updates), and you can arrow up to the text to hear it again before pressing OK, the same way the confirmation message boxes work. Until now only the OK button was readable and the message itself was silent. When a popup carries several notifications at once each line is read, and only the lines that actually apply are read.
- The "Schliess." (Close) button no longer shows up when you arrow through a sub-screen. Every menu's close button does exactly what Escape already does, so landing on it was just an extra dead stop on the way down the list. It's now skipped in Character, Abilities, Inventory, Equipment, the Journal and its Quest Items screen, the shop, every Options screen, and the rest — in all languages. Escape still closes each screen as before, and confirmation popups keep their Cancel / No button (only the standalone Close button is removed).
- The journal's quest list stays readable after you sort it or swap between active and completed quests. Previously a sort could leave entries reading as "control 1", "control 2", … and Enter would stop reading the quest text; both work again now, every time you re-sort or swap.
- The Quest Items screen — opened from inside the journal — is now accessible. Its title is read when you open it, and Enter (or Shift+Up / Shift+Down) on a quest item reads that item's description, the same way item tooltips read elsewhere.
- The journal button that opens the Quest Items screen had an unclear stock label ("Aus Auftrag" in German); it now reads a clearer term in your language.
- The in-game Fähigkeiten screen (your skills, feats and Force powers) is now accessible. Its title is read when you open it; Up / Down move between the Skills, Feats and Powers tabs and Left / Right move along the entries within a tab, with each entry's name and rank read as you go. Enter (or Shift+Up / Shift+Down) reads the focused entry's full description, the same way item and quest tooltips read elsewhere. Escape steps back one level — from an entry out to the tab row, and from there out of the screen.
- You can now take an item off directly from the equipment picker. Open a slot, arrow to the item marked as currently worn (read with "(Ausgew.)"), and press Enter — it's removed and you hear "Ausrüstung abgelegt". Pressing Enter on any other item still equips it as before, so the same key both swaps and removes. No extra "empty" entry was added to the list.

<h2>v0.2.1</h2>

<h3>Stability:</h3>

- The occasional few-second freeze during menu navigation — where the game and sound would lock up for a moment and then recover — should now be fixed. The mod was doing far too much background logging work on every frame; that has been cut back massively, which removes the stall.

<h3>Audio:</h3>

- New "Lautstärke der Hinweistöne" (hint-sound volume) slider under Mod-Einstellungen. Left / Right adjust the volume of every mod cue — wall, door, NPC, container, item and transition cues, the navigation beacon, collisions, combat and cycle cues — in 10% steps, from 100% down to off. Each step plays a short preview at the new level so you can hear the change. Starts at 100%.
- The mod's cues now play at full volume by default. They were previously running at about 83% of unity (they were assigned a quieter engine audio channel), so they could sit too low against ambient and footstep audio; they now ride a dedicated full-volume channel.
- The map-screen edge cue (played when the map cursor hits the edge of the explorable area) is now actually audible. It was being silenced by the map screen's audio pause; it now plays on the same channel the game keeps alive for menu clicks.

<h3>Mod settings:</h3>

- Your Mod-Einstellungen choices now persist between sessions. Until now every option — extended cycling, room shape descriptions, wall sounds, human-speaker subtitles, and the new hint-sound volume — silently reset to its default on each launch. They're now saved to `acc_settings.ini` in the game folder and restored on startup. (Skipping intro movies already persisted, because it's stored as the actual renamed movie files rather than as a setting.)

<h3>Startup:</h3>

- The post-intro main menu no longer keeps warning keyboard-only players that the game is "still loading" — and telling them to use the Alt+F4 → cancel-dialog workaround — after the menu is already responsive. The "input pump is live" handoff was keyed on a focus event that only the mouse produces, so on a keyboard-only machine the loading nag could re-fire on every keypress even once navigation worked. It now clears the moment your first menu keypress is handled, which always happens under keyboard navigation.

<h3>Bug fixes:</h3>

- Saving no longer crashes the game for some players. On certain graphics setups (notably with "Frame Buffer Effects" turned off in the game's options) the engine failed to capture the little save-slot preview image and crashed while trying to shrink a zero-sized picture. The mod now detects that case and hands the engine a blank preview instead, so the save completes normally. The save itself was never the problem — only the thumbnail — so your saves are unaffected apart from a blank slot image on those setups.
- Opening locked doors — and other Shift+Enter action-menu choices — is now reliable. The action you picked, such as "Security" on a locked door, would often fail to fire (you'd hear "this object is locked" or nothing happened) and only worked if you happened to confirm it within a split second; it was especially flaky in combat or when playing in a window. Internally the action menu is now re-anchored to your chosen target on every keypress instead of trusting the engine's shared menu, which the game constantly re-points at whatever the mouse or the combat targeting is on. You can now take your time on the menu and it will still do what you selected.

<h2>v0.2.0</h2>

<h3>Minigames:</h3>

- Pazaak is now playable end-to-end with the keyboard and screen reader. The board reads out every card you and the opponent draw or play, both running totals (with an over-twenty warning), stands, and each set and match result. Up / Down move between zones (your hand, your table, the opponent's table, the Stand / End-turn actions); Left / Right move within a zone (skipping empty hand slots); Enter plays the focused hand card or activates the focused action. S stands, E ends your turn, C reads your hand, T reads both tables with totals, and Shift+C reports how many cards the opponent still holds. Plus/minus flip cards open a sign chooser (Left / Right pick plus or minus, Enter plays with that sign, Esc cancels). The pre-game wager screen now has a top row that reads your bet, the table maximum, and your credits, and announces the bet as you change it; the side-deck builder reads every card and slot.

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

<h3>In-world navigation:</h3>

- New hotkeys Ctrl+`,` and Ctrl+`.` jump straight to the first (closest) and last (farthest) item in the current cycling category, instead of stepping one item at a time to reach an end.

<h3>Audio:</h3>

- The Audio-Glossar (Mod-Einstellungen → Audio-Glossar) now plays its cue previews from the in-game menu too, not just the title-screen options. Arrowing through the list auditions the focused cue; previously every preview was silent once a save was loaded because the in-game menu's pause muted it. The preview now rides the same priority channel as the engine's own GUI sounds, which stays audible through that pause.

<h3>Bug fixes:</h3>

- Shops now announce trade results correctly. Buying or selling a stacked or multi-stock item (e.g. one of several medpacs, or a merchant row with stock > 1) used to say "Kann nicht gekauft/verkauft werden" even though the trade went through — the result was inferred from the item-list row count, which only changes when a stack hits zero. Outcome is now read from your credit balance, which always moves by the price. A second, intermittent case where some items (Computersonden, Reparatur-Parts) silently refused to buy is also fixed: the engine's buy/sell handler ignores rows that aren't flagged active, which keyboard navigation didn't always set.
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
