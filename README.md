---
layout: default
title: KOTOR 1 Accessibility Mod
permalink: /
---
<h1>KOTOR 1 Accessibility Mod</h1>

<h2>What is this mod</h2>

A screen-reader and keyboard-navigation mod for **Star Wars: Knights of the Old Republic 1** (BioWare, 2003, Steam release) that lets fully blind players play KOTOR 1 with any modern screen reader. Speech is routed through the Prism speech bridge, which supports every major screen reader on every major platform.

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

1. Download `KotorAccessibilityInstaller.exe` from the latest release on GitHub
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

- Enter — Trigger the default action on the currently narrated target (same as Mouse 1 click in the world)
- Shift+Enter — Open the radial action menu on the current target
- Shift+H — Read the Examine panel for the current target
- Shift+S — Read the selected character's full stat block
- Shift+L — Open the level-up panel
- Shift+K — Open the combat-queue submenu (review or clear queued actions)

<h3>Mod keys — in-world cycle</h3>

A parallel target cycle that also covers doors, containers, area transitions, and map markers — things the game's Q / E does not pick up.

- `,` / `.` — Previous / next item in the current category
- Shift+`,` / Shift+`.` — Previous / next category (creatures, doors, containers, transitions, map pins, …)
- `-` (German layout) or `/` (US layout) — Announce the currently focused cycle target
- Shift+`-` — Autowalk to that target
- Ctrl+`-` — Arm an audio beacon that pings the way to the target as you move

<h3>Mod keys — orientation and party</h3>

- AltGr (right Alt, alone) — Speak the current facing as a compass direction
- N — Turn the camera 90° clockwise to the next cardinal direction; if a beacon is armed, point at the beacon's next waypoint instead
- Tab — Announces the new party leader after the engine cycles control

<h3>Mod keys — view mode</h3>

Press B to enter view mode. While view mode is active:

- A / D — Pan the camera without moving the character
- Enter — Interact with whatever the camera is pointing at, or autowalk to that point
- Shift+Enter — Force-open the radial on the camera target
- B again — Leave view mode

<h3>Mod keys — map screen</h3>

While the in-game map is open:

- Arrow keys / Up / Down — Cycle through the map's notes and landmarks
- `,` / `.` — Cycle map pins (same vocabulary as the in-world cycle)
- Shift+N — Drop a personal map marker at the cursor's current world position (auto-named after the nearest room or landmark). The new pin joins the cycle immediately and Ctrl+`-` will beacon to it

<h3>Mod keys — submenus</h3>

When a mod submenu is open (action-bar submenu, combat-queue submenu, radial menu):

- Up / Down — Move focus
- Left / Right — Move focus across the radial's 4×3 grid
- Enter — Activate the focused row
- Shift+Enter — (combat-queue only) Clear all queued actions
- Esc — Close the submenu

<h3>Mod keys — context-specific</h3>

- Q or E inside a Container panel — Take all / give items
- Q or E inside a Store panel — Switch between Buy and Sell

Inside the chargen name field (and other text-input boxes):

- Up / Down — Re-read the current text from the start
- Enter — Submit
- Esc — Cancel

<h2>Navigation systems at a glance</h2>

KOTOR is a 3D RPG, so most of the play time is spent moving a character through rooms and around objects. The mod gives you several ways to navigate, layered on top of each other.

<h3>Target cycling (Q / E)</h3>

The game's built-in target cycle. Picks up creatures, doors, and usable placeables within roughly 30 meters that the camera can see. Q steps left, E steps right. Whatever is targeted is what R, 1, 2, 3, and the action bar act on. The mod narrates each new target.

<h3>In-world cycle (`,` / `.`)</h3>

A second cycle the mod adds on top of Q / E. Covers what Q / E misses — area transitions, waypoints, your own map pins — and groups everything by category so you can scan one kind of thing at a time. Use `-` to announce, Shift+`-` to autowalk there, Ctrl+`-` to arm an audio beacon.

<h3>Room shape descriptions</h3>

When you enter a new room the mod speaks the area name, the room's shape (corridor, junction, dead-end, open space), and the exits visible from your current spot. The shape is computed live from the game's walk-mesh, not from a hand-authored description, so it stays accurate across every area in the game.

<h3>Wall-distance audio cues</h3>

A continuous 3D audio layer plays soft positional clicks at the nearest walls around you. The closer the wall, the louder the cue; the further you are, the quieter. As you move, the cues shift in pitch, volume, and direction, giving you a constant spatial sense of where the walls are without needing to query them. This is the mod's main "see the room" feature and is most useful for cramped indoor areas.

<h3>View mode (B)</h3>

A "look without walking" mode. Press B and your W / S movement freezes; A / D now pans the camera freely without rotating your character. From here you can point the camera at a distant object, press Enter to autowalk there, or Shift+Enter to open the radial on it. Useful for distant placeables that don't sit in the Q / E or `,` / `.` cycle, and for surveying a large room.

<h3>Map mode (M)</h3>

KOTOR's in-game map. The mod makes every map pin (doors, transitions, quest markers, your own Shift+N markers) cycleable with `,` / `.` and announced with the same vocabulary used in the world. Fog of war is respected — unexplored pins stay hidden until you've seen them. Shift+N drops a personal marker at the cursor that survives until the panel closes and joins the cycle immediately.

<h2>Reporting bugs</h2>

The installer's post-install screen has a **Collect logs** button that zips the most recent patch log and any Windows Error Reporting dump into your Downloads folder. Attach that zip to a [GitHub issue](https://github.com/JeanStiletto/kotor-blind-accessibility/issues) and describe what you were doing. If you can reproduce a crash, mention which area you were in — the room or area announce will have said it just before.

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

Translations of this page will appear here once available. To help translate, see [CONTRIBUTING.md](CONTRIBUTING.md).
