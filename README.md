---
layout: default
title: Voice of the Old Republic
permalink: /
---
<h1>Voice of the Old Republic</h1>

<p><strong>English</strong> · <a href="/voice-of-the-old-republic/docs/README.de.html">Deutsch</a> · <a href="/voice-of-the-old-republic/docs/README.fr.html">Français</a> · <a href="/voice-of-the-old-republic/docs/README.it.html">Italiano</a> · <a href="/voice-of-the-old-republic/docs/README.es.html">Español</a> · <a href="/voice-of-the-old-republic/docs/README.pl.html">Polski</a> · <a href="/voice-of-the-old-republic/docs/README.ru.html">Русский</a></p>

<h2>What is this mod</h2>

**Voice of the Old Republic** is a project to make the Star Wars: Knights of the Old Republic games accessible for fully blind players. It uses the Prism speech bridge to support every screen reader, adds navigational helpers, menu accessibility and special sound cues to make minigames accessible.

At the current state Knights of the Old Republic I is fully playable and completable, with all minigames, quests and playstyles. It can be played with the keyboard or with an Xbox-style controller — the mod ships its own controller support (see the controller section below); only the minigames still expect the keyboard.

The port for part II is being worked on.

The mod is translated into all supported languages — English, German, French, Italian, Spanish — and supports a Polish and a Russian translation. It supports the 1.03 Steam and GoG version and the 2004 version that is used by the Polish and Russian translations.

<h2>What are the Knights of the Old Republic games</h2>

Knights of the Old Republic is a pair of story-driven Star Wars role-playing games set about four thousand years before the films. The first one was made by BioWare in 2003; the second — Knights of the Old Republic II: The Sith Lords — by Obsidian in 2004, and it plays a few years after part I. Both run on the same engine, which is why the mod can carry over from one to the other.

In both games you create your own character, gather a party of companions, travel between planets, and shape the story through your dialogue choices and whether you follow the light or dark side of the Force.

Combat works the same way in both: real-time with a pause, built on the tabletop Star Wars d20 rules — you queue actions and the dice resolve them. Each round needs 6 real seconds and you can pause at any time to examine the battlefield and queue special actions that are way stronger than the character's auto attacks. You can queue up to 4 actions on each character before you have to let the fight run along, and you can cancel some of the actions if events of the fight require you to change your strategy.

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
3. Right-click the installer and choose **Run as administrator**. On the first run Windows SmartScreen will warn about an "Unknown publisher" — click **More info → Run anyway**. The installer is not code-signed yet, so this warning is expected (see [Troubleshooting](#troubleshooting) for how to verify the download)
4. (Recommended) Back up your save folder at `%USERPROFILE%\Documents\Swkotor\saves\` before installing if you have an existing playthrough
5. Step through the installer screens. It will detect your KOTOR install, install the patch framework, deploy the mod, and (by default) bundle the K1 Community Patch plus the widescreen / high-resolution-menus fixes
6. Launch the game from the installer's final screen or from Steam

<h2>Uninstallation</h2>

Run the installer again and choose the uninstall option, or use Add/Remove Programs. The uninstaller removes only this mod's files — K1CP and any other optional mods you chose at install time are left in place. To return to a fully vanilla KOTOR, use Steam's "Verify integrity of game files" or reinstall from GoG after uninstalling.

<h2>First steps</h2>

When you start a new game, KOTOR first walks you through **character creation**: you pick a class (Soldier, Scout, or Scoundrel), a portrait, and adjust your attributes, skills, and feats. The mod reads each panel as you move through it — take your time; nothing is timed.

You then wake on the **Endar Spire**, a Republic ship under attack. This is the game's tutorial area. The mod replaces the game's on-screen tutorial popups with custom keyboard hints written for screen-reader users, so you learn the controls as you go. Follow Trask, your guide, toward the escape pods.

A few habits that make the early game much easier:

- **Find things with Q / E**, and get back to things you have already found with the `,` / `.` cycle (see the keyboard shortcuts below).
- **Press H** at any time to hear your health and status, and **F1** for the full list of keys.
- **Listen to the room.** Entering a room speaks its name, shape, and exits, and a soft audio layer keeps you aware of the nearest walls as you move.
- **Have a look at the settings early.** Press O for the game's Options. Under Gameplay you will find the auto-pause options, which decide when combat stops on its own — worth setting up before your first real fight — and the key mapping.
- **And at the bottom of the Options list: Mod settings.** Nearly everything this mod adds can be adjusted or turned on and off there, the mod's own keys can be re-bound, and the audio glossary plays every sound cue with its name so you can learn what each one means.

After the Endar Spire you reach **Taris**, the first large world, and the story opens up from there.

<h2>Keyboard shortcuts</h2>

The mod keeps the game's default key map intact, with one ergonomic change the installer applies on a fresh install (see the strafe / camera-rotate note below). Anything not listed here behaves as in the unmodded game. The game's own keys can be re-bound in Options → Gameplay → Key Mapping, or directly in `swkotor.ini`. The keys the mod adds can be re-bound in Mod settings → Key bindings.

<h3>Game keys you will use most</h3>

- W / S — Move forward / backward
- A / D — Strafe left / right
- Z / C — Rotate camera left / right (**Y / C** on a German QWERTZ keyboard)
- Q / E — Cycle target left / right
- R — Default action on current target (attack, talk, open)
- 1 / 2 / 3 — Use the three actions on the current target's action menu
- 4 / 5 / 6 / 7 — Use the player's Force power / medpac / item / mine slots
- Tab — Change party leader
- F — Cancel combat, G — Stealth, V — Solo mode, X — Flourish weapon
- Spacebar — Pause
- Esc — Game menu
- F4 — Quick save, F5 — Quick load (on the main menu, F5 instead checks for a mod update)
- I — Party inventory, U — Equip, P — Player record, K — Skills / feats / powers
- M — Map, L — Quests, J — Messages, O — Options
- Mouse 1 — Click in the 3D world (rarely needed; see view mode below)

> **Strafe / camera-rotate:** KOTOR's own defaults are Z / C to strafe and A / D to rotate the camera. The accessibility installer swaps these on a fresh full install so the two form a comfortable bottom-row cluster — **strafe on A / D, camera rotate on Z / C**. That is the mapping listed above. If you updated from an existing install, or you rebind them yourself in `swkotor.ini`, your keys may differ.

<h3>Mod keys — world interaction</h3>

- Enter — Trigger the default action on the currently narrated target (same as a Mouse 1 click in the world)
- Shift+Enter — Open the unified action menu for the current target (every action — attack, talk, Force powers, items, special abilities — in one menu)
- Shift+1 … Shift+7 — Open one action category to choose from it (1–3 are the target's actions, 4–7 your Force powers / items / mines)
- H — Announce your own health, active effects, and equipped weapon
- Shift+H — Open the action queue (review or clear queued actions)
- Shift+L — Open the level-up panel
- F1 — Open or close the full key list; Ctrl+F1 — read the keys for the current screen
- Ctrl+R — Copy the last spoken text to the clipboard (a dialogue line, a journal entry, an NPC name — whatever was read out last)

<h3>Mod keys — discovered-object cycle</h3>

A second cycle, on top of Q / E, that steps through the objects you have already discovered in the current area — doors, containers, characters, area transitions, landmarks, and your own map markers — grouped by category. (Turn on "Map-wide object selection" in Mod settings to also include things you haven't found yet.)

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

On the pre-game wager screen, the top entry reads your current bet, the table maximum, and your credits; move to "Decrease wager" / "Increase wager" and press Enter to change the bet, then the game's wager button to place it. The side-deck builder reads every card and deck slot.

<h2>Controller</h2>

The mod ships its own controller support — KOTOR 1 has no gamepad code of its own, so the mod drives the pad directly. Any XInput controller with the Xbox button layout works; plug it in before launching the game. Keyboard and controller stay active side by side, and every game action the pad fires goes through your own key bindings, so rebinds are honoured. With a pad connected, the key list (F1) gains a Controller section; without one, that section disappears. The minigames (Pazaak, swoop racing, the turret) have no pad bindings yet and keep their keyboard keys.

The button names below follow the Xbox layout: A, B, X, Y, the left and right shoulder buttons (LB / RB), the left and right triggers (LT / RT), and pressing a stick (L3 / R3).

<h3>Movement and orientation</h3>

- Left stick — Move (all eight directions: forward, backward, and strafing)
- Right stick — Rotate the camera
- R3 (press the right stick) — Turn the camera to the beacon's next waypoint, otherwise to the next compass direction (the keyboard's N)
- Right trigger alone — Announce your facing in degrees

<h3>Objects and actions</h3>

- D-pad left / right — Previous / next discovered object (the keyboard's `,` / `.` cycle)
- D-pad up / down — Previous / next object category
- A — Default action on the focused target (attack, open, talk, pick up)
- B — Close the action menu if it is open; otherwise the engine's own cancel
- Left / right shoulder button — Cycle targets left / right (the keyboard's Q / E)
- Left trigger alone — Open the unified action menu; press it again to close. While it is open: D-pad left / right change the action category, up / down the entry, A fires the selected action
- Left trigger + left shoulder button — Audio beacon to the focused object
- Right trigger + right shoulder button — Autowalk to the focused object

<h3>Party, status, and game</h3>

- X — Switch party leader
- Left trigger + X — Your own status (the keyboard's H)
- Right trigger + X — The action queue (the keyboard's Shift+H). Inside it: D-pad up / down step through the entries, A removes the last queued action, left trigger + A clears the whole queue, B closes
- Y — Quick menu, with the entries: the menu screens, party leader, solo mode, stealth, quick save, and Help, which opens the mod's key list. Walking stops while it is open
- L3 (press the left stick) — Flourish weapon
- Start — Pause
- Back button — Options
- Both triggers (LT + RT) — Read the keys for the current screen (the keyboard's Ctrl+F1)

<h3>In menus</h3>

In the game's menus the pad simply is the keyboard:

- D-pad or left stick — Move focus
- A — Confirm, B — Back
- Left / right shoulder button — Step through the in-game menu's sub-screens (equipment, map, quests, …) — this is how the pad reaches the map; in a container or store they switch the mode instead (take / give, buy / sell)
- Hold Y and press D-pad up or down — Read the focused item's full description, block by block, without moving
- On the map screen the left stick pans the map cursor and the D-pad cycles the map pins

<h2>Navigation systems at a glance</h2>

KOTOR is a 3D RPG, so most of your time is spent moving through rooms and around objects. The mod layers a few systems to keep you oriented — each one narrates itself as you use it.

<h3>Target cycling — Q / E</h3>

Your main way to find and act on things. Q / E step through the creatures, doors, and usable objects the camera can see; whatever is targeted is what Enter and the 1–7 action keys act on. The mod speaks each new target.

<h3>Discovered-object cycle — `,` / `.`</h3>

For getting back to things you've already found. `,` / `.` step through every object you've discovered in the current area — doors, containers, characters, transitions, landmarks, your own markers — grouped by category. Announce one, autowalk to it, or arm an audio beacon. (Mod settings → "Map-wide object selection" widens it to also include things you haven't found yet.)

<h3>Unified action menu — Shift+Enter</h3>

One menu holding every action for the current target — attack, talk, Force powers, items, special abilities. Arrow keys move through it, Enter activates. It replaces the game's separate radial, target, and personal menus.

<h3>Map — M</h3>

KOTOR's in-game map, made navigable. Move the cursor with the arrow keys to read terrain and markers, or cycle the map's pins with `,` / `.` in the same vocabulary used in the world. Fog of war is respected, and Shift+N drops a personal marker at the cursor.

<h3>Wall cues and room-shape descriptions</h3>

As you move, a continuous 3D audio layer plays soft positional clicks off the nearest walls — closer walls sound louder — so you keep a constant feel for the space around you. And entering a room speaks its name, its shape (corridor, junction, dead-end, open space), and the visible exits, all computed live from the game's walk-mesh.

<h2>Mod settings</h2>

Nearly every system the mod adds can be adjusted or turned on and off while you play. Open the game's Options screen (O) and choose **Mod settings** at the bottom of the list. Up / Down move through the rows, Enter toggles a setting or opens a submenu, Left / Right move a slider, and Esc goes back.

- Map-wide object selection — widens the `,` / `.` cycle to objects you have not discovered yet
- Room shape descriptions — the spoken room name, shape, and exits when you enter a room
- Wall sounds — the continuous positional wall-cue layer
- Read voiced-speaker subtitles — speak the subtitles of lines that are voice-acted
- Autoaiming — the aim assist in the turret minigame
- Skip launch intro movies — takes effect on the next launch
- Hint sound volume and Spoken announcement volume — volume sliders for the mod's own sounds and speech
- Key bindings — re-bind the keys the mod adds
- Audio glossary — plays every sound cue the mod uses, one at a time with its name, so you can learn what each sound means

<h2>Troubleshooting</h2>

<h3>No speech after launching the game</h3>

- Make sure your screen reader is running before you launch KOTOR.
- The Prism speech runtime ships with the mod and is placed automatically by the installer. If you installed manually, make sure its files are present in the game folder.
- Check the newest patch log at `<install>\logs\patch-*.log` for errors (the installer's **Collect logs** button gathers it for you).

<h3>Game crashes on startup, or the mod doesn't load</h3>

- Run the installer as administrator — it deploys the `dinput8.dll` proxy that auto-loads the mod when the game starts.
- Confirm your game version is supported (see "Game versions not supported" above). The installer checks the `swkotor.exe` hash and will tell you if it doesn't match.
- If the game updated recently, re-run the installer — an update can overwrite the loader.

<h3>The mod was working but stopped after a game update</h3>

- Steam and GoG updates can overwrite the mod's loader files. Run the installer again to redeploy the mod.

<h3>Keyboard shortcuts not working</h3>

- Make sure the game window is focused (Alt+Tab to it).
- Press F1. If you hear the key list, the mod is active.
- Some keys only work in a specific context (the Pazaak keys only work on the Pazaak board, the submenu keys only inside a mod submenu, and so on).

<h3>The camera no longer turns with the keys</h3>

- You have probably switched on the game's free look mode. The game's default key map lists Caps Lock as the free look toggle, so it is easy to hit by accident — try pressing Caps Lock again. Otherwise check the mouse and camera settings in Options.
- Also check that you are not in the mod's view mode (B), where A / D pan the camera instead of moving your character. Press B again to leave it.

<h3>Wrong language</h3>

- The mod picks its language automatically from your game's language (read from the game's `dialog.tlk`). There is no in-game language switch yet, so to change the mod's language, install the game in that language. The five languages KOTOR ships in — English, French, German, Italian, Spanish — are supported.

<h3>Windows warns the installer or the DLL is unsafe</h3>

The installer and the mod are not code-signed. Code-signing certificates cost hundreds of euros per year, which is not realistic for a free accessibility project, so Windows SmartScreen will warn you the first time you run the installer and may flag the files as coming from an unknown publisher.

To verify the file you downloaded matches the one published on GitHub, each release lists a SHA-256 checksum. You can compute the hash of your download and compare:

- PowerShell: `Get-FileHash <filename> -Algorithm SHA256`
- Command Prompt: `certutil -hashfile <filename> SHA256`

If the hash matches the one in the release notes, the file is genuine. To run past the SmartScreen warning, choose "More info" and then "Run anyway."

<h2>Reporting bugs</h2>

The installer's post-install screen has a **Collect logs** button that zips the most recent patch log and any Windows Error Reporting dump into your Downloads folder. Attach that zip to a [GitHub issue](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) and describe what you were doing. If you can reproduce a crash, mention which area you were in — the room or area announce will have said it just before.

<h2>Known issues</h2>

For the current backlog of bugs, planned features, and rough edges, see [docs/known-issues.md](docs/known-issues.md).

<h2>Disclaimers</h2>

<h3>Other accessibilities</h3>

For now, this is a screen-reader accessibility mod. I am a fully blind developer, and screen-reader access is the area I know. I would genuinely like to cover more disabilities — low vision, motor impairments, and so on — but questions like colour, contrast, and font are abstract to me as a fully blind person. If you need something in that direction and can describe your needs clearly and help test the result, please get in touch. I would be happy to make the mod live up to its name for more people.

<h3>AI use</h3>

The code of this mod is written with heavy assistance from Anthropic's Claude, using the Opus models (development spanned the Opus 4.5, 4.6, and 4.7 generations). I am aware of the debates around AI-assisted development. But at a time when the games industry has never delivered the accessibility we need — in quality or in quantity — for titles like KOTOR, these tools are what make a project this size feasible for a single blind developer. Every change is reviewed and tested in-game, by ear, before it ships.

<h2>How to contribute</h2>

Contributions are welcome — especially fixes for languages, system configurations, or screen readers I cannot test locally. Take feature requests too. Before starting work, skim the known-issues file above to see if your idea is already on the backlog.

- Contribution guide: [CONTRIBUTING.md](CONTRIBUTING.md)
- Architecture overview: [ARCHITECTURE.md](ARCHITECTURE.md)
- Accessibility modding guide (general craft + native-binary specifics): [ACCESSIBILITY_MODDING_GUIDE.md](ACCESSIBILITY_MODDING_GUIDE.md)
- Translating the mod's spoken cues: [docs/CONTRIBUTING_TRANSLATIONS.md](docs/CONTRIBUTING_TRANSLATIONS.md)

<h2>Credits</h2>

And now I want to thank a whole lot of people. First, this mod relies massively on community work, and many people did the really hard things, so I just had to pick their tools up and create my own thing with them. Further, thankfully, this was not just me and the AI in a black box, but a whole network around me, helping out, empowering, just being social and nice.

Please DM me if I forgot you, or if you want to be known under a different name or not mentioned at all.

The reverse-engineering and patch framework this mod runs on top of come from **Lane Dibello**, whose [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) and Ghidra work made it possible to hook the game at all. Thanks also to the KOTOR modding community around **DeadlyStream**, who figured out the tools and formats I only had to pick up and use.

A lot of people helped me testing the game, found all the bugs I couldn't find, and gave me feedback to pinpoint the weaknesses and inconveniences of the project. Without them it could not have become a project at all. So I want to thank:

- Kenny
- Berenion
- unexplained entity
- Grinvold
- dansc93
- Mojsior
- stirlock
- Druidah
- SightlessKombat
- Destranis
- Ozuaw
- mkdbzfan
- Kamilana
- ABlindFellow
- zargontheevilgod
- zersiax

**Foundations and dependencies:**

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), the reverse-engineered Ghidra database, and the patch framework
- **Prism** (Ethin P.) — cross-platform speech bridge covering every major screen reader, with SAPI fallback
- **K1 Community Patch** team (KOTORCommunityPatches) — bundled bug-fix layer
- **xoreos / xoreos-tools** — open-source engine reimplementation; cross-reference for file formats
- **DeadlyStream community** — modding knowledge base

<h3>Tools used</h3>

- Claude (Anthropic) — pair-programming partner across the Opus 4.5, 4.6, and 4.7 generations
- Kotor-Patch-Manager — runtime DLL-injection patch framework
- Prism — screen-reader speech bridge
- Tolk — screen-reader library (fallback path)
- Ghidra — reverse-engineering
- xoreos-tools — headless extraction of game file formats
- K1 Community Patch — bundled community bug-fix layer

<h2>Support your modder</h2>

Building this mod has been a lot of fun and a real source of empowerment, but it also cost a lot of time and real money in Claude subscriptions. I intend to keep those running to maintain the project and improve it over the coming years. If you are able and willing to make a one-time or recurring donation, I would deeply appreciate it — it recognises the work and gives me a stable base to keep improving Voice of the Old Republic and, hopefully, other large accessibility projects.

[Ko-fi: ko-fi.com/jeanstiletto](https://ko-fi.com/jeanstiletto)

<h2>License</h2>

The mod source is licensed under the GNU General Public License v3 (see [LICENSE](LICENSE)). Vendored dependencies under `third_party/` keep their own licenses (Prism is MPL-2.0; Tolk is LGPL; Kotor-Patch-Manager is bundled per its upstream terms; dsoal and OpenAL Soft, when the optional spatial-audio path is enabled, are LGPL-2.1). The game itself and BioWare's data files are not redistributed by this project.

<h2>Links</h2>

- [GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic)
- [Report an issue](https://github.com/JeanStiletto/voice-of-the-old-republic/issues)
- [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager)
- [K1 Community Patch (DeadlyStream)](https://deadlystream.com/)
- [Ko-fi (support the project)](https://ko-fi.com/jeanstiletto)

<h2>Other languages</h2>

- [Deutsch](/voice-of-the-old-republic/docs/README.de.html)
- [Français](/voice-of-the-old-republic/docs/README.fr.html)
- [Italiano](/voice-of-the-old-republic/docs/README.it.html)
- [Español](/voice-of-the-old-republic/docs/README.es.html)
- [Polski](/voice-of-the-old-republic/docs/README.pl.html)
- [Русский](/voice-of-the-old-republic/docs/README.ru.html)

Translations are kept in `docs/README.{de,fr,it,es,pl,ru}.md`. To improve or add a translation, see [docs/CONTRIBUTING_TRANSLATIONS.md](docs/CONTRIBUTING_TRANSLATIONS.md).
