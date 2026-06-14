---
layout: default
title: Voice of the Old Republic
permalink: /
---
<h1>Voice of the Old Republic</h1>

<p><strong>English</strong> · <a href="/voice-of-the-old-republic/docs/README.de.html">Deutsch</a> · <a href="/voice-of-the-old-republic/docs/README.fr.html">Français</a> · <a href="/voice-of-the-old-republic/docs/README.it.html">Italiano</a> · <a href="/voice-of-the-old-republic/docs/README.es.html">Español</a></p>

<h2>What is this mod</h2>

**Voice of the Old Republic** is a screen-reader and keyboard-navigation mod for **Star Wars: Knights of the Old Republic 1** (BioWare, 2003, Steam release) that lets fully blind players play KOTOR 1 with any modern screen reader. Speech is routed through the Prism speech bridge, which supports every major screen reader on every major platform.

The mod is written by a blind developer. Every workflow — installing, playing, contributing — is designed to be doable with a screen reader and keyboard alone.

<h2>Requirements</h2>

- Windows 10 or later
- Star Wars: Knights of the Old Republic, v1.0.3 (Steam or GoG; both are byte-identical for our purposes)
- A screen reader. Speech is routed through Prism, which supports the full set of screen readers in active use; if your screen reader works with anything else on your system, it will work with this mod
- About 200 MB of free disk space for the patcher runtime, the K1 Community Patch, and the bundled speech runtime

<h3>Game versions not supported in this release</h3>

- Aspyr mobile / macOS ports (different binary)
- Pre-patched executables (UniWS-modified, KOTOR High-Resolution Menus-modified)
- Builds whose `swkotor.exe` SHA-256 doesn't match the recognised Steam or GoG 1.0.3 hashes

If the installer reports a version mismatch, file an issue with the displayed hash. The address database covers both Steam and GoG out of the box, and adding a new byte-equivalent re-pack is usually a one-line manifest change.

<h2>Installation</h2>

1. Download `VoiceOfTheOldRepublicInstaller.exe` from the latest release on GitHub
2. Close KOTOR if it is running
3. Right-click the installer and choose **Run as administrator**. On the first run Windows SmartScreen will warn about an "Unknown publisher" — click **More info → Run anyway**. The installer is not code-signed yet, so this warning is expected
4. (Recommended) Back up your save folder at `%USERPROFILE%\Documents\Swkotor\saves\` before installing if you have an existing playthrough
5. Step through the installer screens. It will detect your KOTOR install, install the patch framework, deploy the mod, and (by default) bundle the K1 Community Patch plus the widescreen / high-resolution-menus fixes
6. Launch the game from the installer's final screen or from Steam

To uninstall, run the installer again and choose the uninstall option, or use Add/Remove Programs. The uninstaller removes only this mod's files — K1CP and any other optional mods you chose at install time are left in place. To return to a fully vanilla KOTOR, use Steam's "Verify integrity of game files" or reinstall from GoG after uninstalling.

<h2>Keyboard shortcuts</h2>

The mod keeps the game's default key map intact. Anything not listed below behaves as in the unmodded game. Every action below can be re-bound in `swkotor.ini` (game keys) or, for mod-added keys, will be re-bindable from an in-game settings screen in a later release.

<h3>Game keys you will use most</h3>

- W / S — Move forward / backward
- A / D — Rotate camera left / right
- Z / C — Strafe left / right
- Q / E — Cycle target left / right
- R — Default action on current target (attack, talk, open)
- 1 / 2 / 3 — Use the three actions on the current target's action menu
- 4 / 5 / 6 / 7 — Use the player's Force power / medpac / item / mine slots
- Tab — Change party leader
- F — Cancel combat, G — Stealth, V — Solo mode, X — Flourish weapon
- Spacebar — Pause
- Esc — Game menu
- F4 — Quick save, F5 — Quick load
- I — Party inventory, U — Equip, P — Player record, K — Skills / feats / powers
- M — Map, L — Quests, J — Messages, O — Options
- Mouse 1 — Click in the 3D world (rarely needed; see view mode below)

<h3>Mod keys — world interaction</h3>

- Enter — Trigger the default action on the currently narrated target (same as a Mouse 1 click in the world)
- Shift+Enter — Open the unified action menu for the current target (every action — attack, talk, Force powers, items, special abilities — in one menu)
- Shift+1 … Shift+7 — Open one action category to choose from it (1–3 are the target's actions, 4–7 your Force powers / items / mines)
- H — Announce your own health, active effects, and equipped weapon
- Backtick (US layout) / Ö (German layout) — Read the Examine panel for the current target
- Shift+H — Open the action queue (review or clear queued actions)
- Shift+L — Open the level-up panel
- F1 — Open or close the full key list; Ctrl+F1 — read the keys for the current screen

<h3>Mod keys — discovered-object cycle</h3>

A second cycle, on top of Q / E, that steps through the objects you have already discovered in the current area — doors, containers, characters, area transitions, landmarks, and your own map markers — grouped by category. (Turn on "Extended cycling" in Mod Settings to also include things you haven't found yet.)

- `,` / `.` — Previous / next object in the current category
- Shift+`,` / Shift+`.` — Previous / next category (creatures, doors, containers, transitions, map pins, …)
- Ctrl+`,` / Ctrl+`.` — Jump to the nearest / farthest object in the category
- `/` (US layout) or `-` (German layout) — Announce the currently focused object
- Shift+`/` (Shift+`-`) — Autowalk to that object
- Ctrl+`/` (Ctrl+`-`) — Arm an audio beacon that pings the way as you move

<h3>Mod keys — orientation and party</h3>

- AltGr (right Alt, alone) — Speak the current facing as a compass direction
- N — Turn the camera 90° clockwise to the next cardinal direction; if a beacon is armed, point at the beacon's next waypoint instead
- Tab — Announces the new party leader after the engine cycles control

<h3>Mod keys — view mode</h3>

Press B to enter view mode. While view mode is active:

- A / D — Pan the camera without moving the character
- Enter — Interact with whatever the camera is pointing at, or autowalk to that point
- Shift+Enter — Open the action menu on the camera target
- B again — Leave view mode

<h3>Mod keys — map screen</h3>

While the in-game map is open:

- Arrow keys / Up / Down — Cycle through the map's notes and landmarks
- `,` / `.` — Cycle map pins (same vocabulary as the discovered-object cycle)
- Shift+N — Drop a personal map marker at the cursor's current world position (auto-named after the nearest room or landmark). The new pin joins the cycle immediately and Ctrl+`-` will beacon to it

<h3>Mod keys — submenus</h3>

When a mod submenu is open (the unified action menu, a category menu, the action queue):

- Up / Down — Move focus
- Left / Right — Move between columns or variants
- Enter — Activate the focused row
- Shift+Enter — (action queue only) Clear all queued actions
- Esc — Close the submenu

<h3>Mod keys — context-specific</h3>

- Q or E inside a Container panel — Take all / give items
- Q or E inside a Store panel — Switch between Buy and Sell

Inside the chargen name field (and other text-input boxes):

- Up / Down — Re-read the current text from the start
- Enter — Submit
- Esc — Cancel

<h3>Mod keys — Pazaak minigame</h3>

While the Pazaak board is open:

- Up / Down — Move between zones: your hand, your table, the opponent's table, the actions (Stand / End turn)
- Left / Right — Move within the current zone (empty hand slots are skipped)
- Enter — Play the focused hand card, or activate the focused action
- S — Stand
- E — End turn
- C — Read your hand
- T — Read both tables with their totals
- Shift+C — How many cards the opponent is still holding
- Plus/minus flip card — Enter opens a sign chooser; Left / Right pick plus or minus, Enter plays with that sign, Esc cancels

On the pre-game wager screen, the top entry reads your current bet, the table maximum, and your credits; move to "Einsatz verringern" / "Einsatz erhöhen" (decrease / increase) and press Enter to change the bet, then "Setzen" to place it. The side-deck builder reads every card and deck slot.

<h2>Navigation systems at a glance</h2>

KOTOR is a 3D RPG, so most of your time is spent moving through rooms and around objects. The mod layers a few systems to keep you oriented — each one narrates itself as you use it.

<h3>Target cycling — Q / E</h3>

Your main way to find and act on things. Q / E step through the creatures, doors, and usable objects the camera can see; whatever is targeted is what Enter and the 1–7 action keys act on. The mod speaks each new target.

<h3>Discovered-object cycle — `,` / `.`</h3>

For getting back to things you've already found. `,` / `.` step through every object you've discovered in the current area — doors, containers, characters, transitions, landmarks, your own markers — grouped by category. Announce one, autowalk to it, or arm an audio beacon. (Mod Settings → "Extended cycling" widens it to also include things you haven't found yet.)

<h3>Unified action menu — Shift+Enter</h3>

One menu holding every action for the current target — attack, talk, Force powers, items, special abilities. Arrow keys move through it, Enter activates. It replaces the game's separate radial, target, and personal menus.

<h3>Map — M</h3>

KOTOR's in-game map, made navigable. Move the cursor with the arrow keys to read terrain and markers, or cycle the map's pins with `,` / `.` in the same vocabulary used in the world. Fog of war is respected, and Shift+N drops a personal marker at the cursor.

<h3>Wall cues and room-shape descriptions</h3>

As you move, a continuous 3D audio layer plays soft positional clicks off the nearest walls — closer walls sound louder — so you keep a constant feel for the space around you. And entering a room speaks its name, its shape (corridor, junction, dead-end, open space), and the visible exits, all computed live from the game's walk-mesh.

<h2>Reporting bugs</h2>

The installer's post-install screen has a **Collect logs** button that zips the most recent patch log and any Windows Error Reporting dump into your Downloads folder. Attach that zip to a [GitHub issue](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) and describe what you were doing. If you can reproduce a crash, mention which area you were in — the room or area announce will have said it just before.

<h2>Known issues</h2>

For the current backlog of bugs, planned features, and rough edges, see [docs/known-issues.md](docs/known-issues.md).

<h2>Contributing</h2>

Contributions are welcome — especially fixes for languages, system configurations, or screen readers the developer cannot test locally. Before starting work, skim the known-issues file above to see if your idea is already on the backlog.

- Contribution guide: [CONTRIBUTING.md](CONTRIBUTING.md)
- Architecture overview: [ARCHITECTURE.md](ARCHITECTURE.md)

<h2>AI use</h2>

The mod's code is written with heavy assistance from Anthropic's Claude (Opus series). In a games industry that has historically refused to ship native accessibility for titles like KOTOR, AI-assisted modding is what makes a project this size feasible for a single blind developer. Every change is reviewed and tested in-game by the author before it ships.

<h2>License</h2>

The mod source is licensed under the GNU General Public License v3 (see [LICENSE](LICENSE)). Vendored dependencies under `third_party/` keep their own licenses (Prism is MPL-2.0; Tolk is LGPL; Kotor-Patch-Manager is bundled per its upstream terms; dsoal and OpenAL Soft, when the optional spatial-audio path is enabled, are LGPL-2.1). The game itself and BioWare's data files are not redistributed by this project.

<h2>Credits</h2>

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), the reverse-engineered Ghidra database, and the patch framework this mod runs on top of
- **Prism** (Ethin P.) — cross-platform speech bridge covering every major screen reader, with SAPI fallback
- **K1 Community Patch** team (KOTORCommunityPatches) — bundled bug-fix layer
- **xoreos / xoreos-tools** — open-source engine reimplementation; cross-reference for file formats
- **DeadlyStream community** — modding knowledge base
- **Claude (Anthropic)** — pair-programming partner across the Opus 4.5, 4.6, and 4.7 generations

<h2>Other languages</h2>

- [Deutsch](/voice-of-the-old-republic/docs/README.de.html)
- [Français](/voice-of-the-old-republic/docs/README.fr.html)
- [Italiano](/voice-of-the-old-republic/docs/README.it.html)
- [Español](/voice-of-the-old-republic/docs/README.es.html)

Translations are kept in `docs/README.{de,fr,it,es}.md`. To improve or add a translation, see [CONTRIBUTING.md](CONTRIBUTING.md).
