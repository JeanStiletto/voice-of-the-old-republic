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

Bullet rules:

- **Lead with the user-facing change** ("X works now", "new hotkey Y", "X no
  longer does Z"); explanation comes after.
- **Scale the explanation to the problem.** Most bullets are one or two
  sentences. Reserve a full before/after/mechanism explanation for the genuinely
  complex — a subtle bug, a non-obvious mechanism, or a real trade-off the user
  needs to know. Simple changes do not earn a paragraph.
- **Cut redundancy — each fact once.** Don't restate the headline in the body,
  don't describe the same behaviour twice, and don't narrate every keypress when
  one clause conveys the contract.
- **Never document unchanged behaviour** ("X is unaffected", "still works as
  before"); omit it. One fix per bullet — split conflated changes.

Write examples in **English**, not the German in-game labels. The release
notes are English-only and most readers are English-speaking, so quote UI
elements in English — "Force Powers" not "Machtkräfte", "Strength" not
"Stärke", "in the party" not "im Team". (The mod itself still speaks the
player's installed language in-game; this only governs how we *describe* it
here.) Where naming the exact spoken string matters, give the English term
and add the German in parentheses if it genuinely aids clarity.

<h2>v0.7.3</h2>

<h3>KOTOR 2:</h3>

- The save screen loads the save you picked. It always loaded the newest one,
  whichever you had arrowed to and heard announced. KOTOR 2 lays this screen
  out differently from KOTOR 1 — the save list and every button but Load sit at
  different places in the panel — so the mod never recognised it as a save
  screen at all, and while the keyboard moved and spoke the rows, nothing told
  the game which row that was. Load then acted on the game's own idea of the
  selection, which never left the top of the list.

- Save slots announce where you were and how long you had played — "Ebon Hawk,
  Interior, Time: 8h 16m" after the slot's name and date. KOTOR 1 has had this;
  on KOTOR 2 the information is not attached to the list row, so it was
  missing entirely.

- Objects whose name contains a placeholder for your character's name say your
  name. The Peragus medical bay's kolto tank announced as "<FullName>'s Kolto
  Tank", reading the placeholder aloud.

- Doors and containers no longer read out the level designer's notes. KOTOR 2
  leaves working annotations in object names — "Blast Door{HK-50}", "Emergency
  Blast Door{103PER}", "Medical Bay Container{Chems}" — and they were spoken
  every time the object was announced, including inside action prompts ("Open
  Medical Bay Container{Chems}").

- Character generation's Feats screen no longer opens by announcing "Items
  Available to Place in Container and blah blah blah". That is a leftover from
  the container transfer screen sitting in the Feats layout, and being the
  first piece of text on the panel it was taken for the screen's title.

<h3>Bug fixes:</h3>

- Pressing Enter on a map landmark walks you there instead of acting on
  something else entirely. Landmarks are waypoints — map markers with no
  physical presence — so the engine has no action for one and quietly answered
  with whatever it had been asked about last. That answer was then carried out:
  usually nothing happened and you heard "movement cancelled, way blocked" a
  second later, but if the last thing asked about was a person, it started a
  conversation with them. Landmarks now walk to the coordinate, the way
  Shift+Minus on the same target already did, and an answer that belongs to a
  different object is no longer accepted from the engine at all.

- Items lying on the ground are named properly and no longer report a failure
  they can't avoid. Such an item usually carries no name of its own — the name
  comes from its template — so it was announced by its internal resource id
  ("g_w_sbrcrstl11"). It now uses the same name a sighted player sees. Objects
  the game genuinely offers nothing for say so once, rather than announcing the
  engine's internal "No Action" label as though it were a verb and then
  reporting that it failed.

- The equipment screen stops calling everything unavailable. While the item
  list for a slot is open the game switches that slot's button off, because the
  list has taken over — and every swap re-read the slot and announced it as
  unavailable. The "unavailable" wording is kept for what it means (an option
  you cannot use) and no longer appears while a picker list is open. Same for
  the workbench.

- Confirmation prompts are read once. A message box's text is both the panel's
  new content and its only selectable row, and the two announcers each spoke
  it, so "Are you sure you want to overwrite the save game?" arrived twice.

- Switching party leader with Tab no longer announces a direction the camera
  never faced. The compass reads the heading as "camera towards character", and
  a Tab swap moves the character half to the new leader a frame or two before
  the camera catches up — so for that moment the reading pointed roughly
  backwards, and you heard the wrong direction followed by a correction. Such
  jumps are now recognised as jumps rather than very fast turns, and the
  direction is spoken once the camera has settled. Tabbing quickly past one
  party member to reach another announces only the one you stop on, and a swap
  between members facing the same way stays silent. The same applies to engine
  camera cuts, which produced the same stray pair of cues.

- Turning the camera continuously no longer fires bursts of direction cues.
  While turning, the announcement is deliberately rate-limited, but the check
  for "the turn has ended" ran off a clock too coarse to measure a single
  frame: often enough it read the time between two frames as zero, concluded
  the camera had stopped, and announced immediately — up to three sectors in
  one second. Timing now comes from a source fine enough to see one frame, so
  a continuous turn produces evenly spaced cues and a single one when you stop.

<h2>v0.7.2</h2>

<h3>KOTOR 2:</h3>

- Workbenches and lab stations can craft. Pressing Enter on a recipe, or the
  screen's own "Create Item" button, did nothing at all — the engine refuses to
  build unless the recipe row carries the internal "active" flag that only a
  real mouse-over sets, so every keyboard attempt was declined in silence. The
  flag is now set the way a click would, and the row you are standing on is
  kept as the engine's selection, so both routes work. Breaking items down for
  materials works the same way.

- The workbench and lab-station item lists were empty about as often as not.
  The test for which of the two lists (create / break down) is currently
  showing was reading uninitialised memory on KOTOR 2, so it answered
  differently from one session to the next, and a list it judged hidden had all
  its rows dropped from keyboard navigation. The same test also decides a
  KOTOR 2 store's buy-versus-sell mode, which was equally unreliable.

- Q and E switch between creating items and breaking them down, matching what
  those keys already do in containers and shops. The screen's two action
  buttons, "View Inventory" and "Create Item", stop appearing as navigation
  stops in exchange: Q and E do the first, and Enter on a recipe already does
  the second. This is the same arrangement as a shop, where Enter on a row
  trades and the Buy/Sell button is hidden — worth knowing if you have watched
  someone play with a mouse, because there it really is two separate clicks.

- Crafting screens announce what a recipe costs and what a teardown returns.
  Arrowing over a recipe speaks "Costs 12" after its name; in the break-down
  list rows speak "Returns 3" — your skill decides how much of the material
  actually comes back, so the two numbers are rarely the same. The screen's
  running total is a navigation row of its own at the top of the list, the way
  credits are in a shop, and names its own resource ("All Components" at a
  workbench, "All Chemicals" at a lab station).

- Recipes your character isn't skilled enough to make say so, as "not
  available" after the cost. The game deliberately lists recipes up to eight
  skill ranks beyond your current one, mixed in with the rest, and answers an
  attempt on one with a message box and nothing built — so a list you can only
  partly use is normal here, and now audible.

- The keyboard comes back after another window takes the focus. On KOTOR 2 the
  mod could never re-grab the engine's DirectInput devices, because the two
  addresses it needs — the input singleton and `CExoInput::SetActive` — were
  only ever filled in for KOTOR 1 and silently resolved to nothing on KOTOR 2.
  Every attempt failed inside the safety net, so the recovery that KOTOR 1 has
  had since v0.5.1 simply did not exist there: after a screen reader or any
  other window stole the foreground, keys could stop reaching the game with no
  way back. The same gap also meant KOTOR 2 never released the keyboard when it
  lost the foreground, so keystrokes meant for another window went on reaching
  the game in the background. Both work now.

<h3>Bug fixes:</h3>

- Russian and Polish: character creation announces the class descriptions
  again. Two entries were missing from those two translation tables, and one of
  them is the sentence pattern that joins a class name to its description — so
  instead of falling back to the plain name, class rows with a description
  announced nothing at all. The other missing entry is KOTOR 2's fourth
  portrait head family, which read as silence in the portrait picker.

<h3>Installer:</h3>

- The Polish-translation dialogs read correctly in German, French, Italian,
  Spanish and Russian. Their text had been through a bad encoding conversion,
  so accented letters arrived as pairs of symbols; the Russian version was
  unreadable throughout rather than merely blemished.

- The three Russian-translation dialogs now appear in German, French, Italian
  and Spanish instead of falling back to English.

- An unsupported game version is now refused with an explanation, in your
  language, instead of a generic "patch application failed" carrying a line of
  English developer text. The mod is tied to the exact program file of each
  release it supports, so the two things worth telling you are which of them
  applies: another mod has already rewritten the game executable — in which
  case restoring it and re-running the installer fixes it — or the release
  simply is not covered yet, which is a bug report. The message names your
  game, the executable, and its fingerprint, so a report carries what is needed
  to add the build. Nothing is written to the game folder before this check.

- "Collect logs for beta test" now bundles both games. It used to collect a
  single game, chosen without asking and without saying which — and the choice
  was always KOTOR 1 when both were installed, so a KOTOR 2 bug report arrived
  with KOTOR 1's log attached. The crash dump had the mirrored problem: its
  filename filter matched either game, so a bundle labelled KOTOR 1 could carry
  KOTOR 2's dump. Each game's newest log and dump are now included, prefixed
  `k1-` / `k2-`, and the bundled system-info file describes both installs, so a
  report says which games are present and how each is set up. Typical cost is a
  few dozen KB, since patch logs compress 10-70x; a bundle carrying a crash
  from both games at once measures 4 MB.

- Crash dumps in that bundle keep the memory a crash report is actually about.
  Dumps are shrunk by dropping the captured memory of modules we never read,
  but the keep-list named only KOTOR 1's executable and never listed the mod's
  own `accessibility.dll` at all. Measured against live dumps of both games, a
  KOTOR 2 dump was arriving 9.4 MB short — the whole game executable plus our
  patch — and a KOTOR 1 dump 3.4 MB short. This never made a dump unreadable
  (stacks, heap, thread contexts and the module list were always kept, which is
  what most diagnosis runs on), but it removed the code and state of the two
  modules most likely to be at fault. Both are kept now, as is the bundled
  widescreen patch.

<h3>Documentation:</h3>

- The README now states plainly where KOTOR 2 stands: the mod installs and
  speaks there, but the game has not been played through to the end yet, some
  blockers still have to be removed, and its minigames and tutorial messages
  are not covered. The supported-version list is also split per game — Aspyr's
  2015 build for KOTOR 2 — where it previously named only the KOTOR 1
  releases. All seven translations carry the same wording.

<h3>Development:</h3>

- The mod now builds with debug symbols, so a crash report can be traced to a
  named function and source line instead of an offset into `accessibility.dll`.
  The symbol file is not shipped to players and does not change what the
  installer downloads — the patch is byte-for-byte the same size as before. The
  release script archives each version's symbols locally, three releases back,
  because symbols only match the exact build they came from and a report often
  arrives from someone who has not updated yet.

<h2>v0.7.1</h2>

<h3>Bug fixes:</h3>

- Russian and Polish KOTOR 1: shortened combat messages work now. Both
  community translations fill the hit/miss tag of the combat stats line, which
  renders the connector after the target with a single space — the parser
  expected the double-space form and every attack summary silently fell back
  to the full raw combat text being spoken.

<h3>KOTOR 2:</h3>

- Shortened combat messages now work in all supported languages, not just
  German. French, Italian and Spanish anchors come from the game's own
  localized text (their TSLRCM translations live inside the dialog files, so
  the base game supplies the combat strings); Russian comes from the Russian
  TSLRCM Workshop translation, which re-translated nearly every combat
  template. English needed no changes — its KOTOR 2 combat text is identical
  to KOTOR 1's. Untested in game yet; any anchor that doesn't match falls
  back to speaking the raw combat text.

- The mod now speaks your installed language instead of always German. Language
  detection ran only from a KOTOR-1-only startup hook, so on KOTOR 2 the string
  table stayed on its compiled-in German default for everyone; detection (and
  the Russian CP1251 / Polish CP1250 speech-codepage pinning that comes with it)
  now runs as part of the one-time speech init both games share, before the
  first spoken word.

<h3>Installer:</h3>

- Downloading the mod survives a flaky GitHub. The v0.7.0 release-day outage
  had github.com intermittently dropping connections while the GitHub API
  stayed up: the installer's single direct-download attempt failed straight
  into the rate-limited API fallback, which can answer 403 on shared (CGNAT /
  VPN) addresses — "the patch file could not be downloaded". The direct
  download and the release-tag lookup now retry with backoff before falling
  back, and one install run resolves the release tag once instead of twice,
  halving the API requests spent when the fallback is needed.

- Russian KOTOR 2 now gets Russian text, not just Russian content files. The
  Russian TSLRCM Workshop edition is the only one that carries its own text
  table — it has to, since the game was never sold in Russian — and the
  installer skipped it under a blanket rule written for the German, French,
  Italian and Spanish editions, which correctly ship none because those
  players already have a localized text table from the game itself. Russian
  players were left reading English for everything outside the translated
  dialog files. The previous `dialog.tlk` is kept as
  `dialog.tlk.pre-tslrcm.bak`.

<h2>v0.7.0</h2>

<h3>KOTOR 2:</h3>

- Swapping weapon sets announces what you now hold — "Weapons switched: Zabrak blaster" ("Waffen gewechselt: …") — whether you swap with the key, the equip screen's swap button, or a script does it. The engine keeps no "set 1 / set 2" state (a swap physically moves the items between the two slot pairs, so your current weapons are always the first pair), which is why the announcement names the weapons instead of a set number.
- The equip screen's two weapon-slot pairs now read as "active" and "secondary" ("aktiv" / "sekundär") instead of the engine's "Konfig 1" / "Konfig 2" — the first pair is always what the character holds, so numbered configs wrongly suggested a persistent set choice.
- The game's Switch Weapon Configuration key moves from H to the key right of L (Ö on a German keyboard, semicolon on US) on a full install, so the mod's H (self status) and Shift+H (action queue) no longer silently swap your weapons on every press. The mod's dev-only Examine panel gave up that key and is now unbound by default (rebindable in the keybind configurator). Updates leave your bindings alone, and an uninstall hands the key back to the game's default.
- Status effects on KOTOR 2 now speak the same rich names as on KOTOR 1. The target brief, the bare-H self status and the examine view name the actual buffs and debuffs a sighted player sees on the portrait — "Energy Shield", "Force Speed", "Battle Meditation", including KOTOR 2's own additions (Force Body, Fury, Force Barrier, the new droid shields) — where they previously fell back to generic engine categories like "State" and "Immunity". The names come from the game's own data in the installed language.
- Health condition works on KOTOR 2. Target briefs and the examine view's condition row speak the wound-state word ("wounded", "badly wounded", …) exactly as on KOTOR 1; before, the condition was silently missing on KOTOR 2.
- The examine view's Level, Faction and Blinded rows and the feat list now fill in on KOTOR 2. Level uses the engine's own reader, so scaled enemy levels match what the game displays. The faction fix also restores hostile/friendly classification everywhere it is used on KOTOR 2.
- Enemy briefs no longer speak placeholder weapon names like "{Prop QS 01}". Some NPC-only built-in weapons carry an unlocalized developer name in the game data — sighted players never see enemy weapon names, so it was never cleaned up. A brace-wrapped name is now treated like an empty hand, in the target brief, the self status, weapon-swap announcements and the examine view.
- Doors that KOTOR 2 marks as scenery but which carry a real lock now announce "locked" instead of "cosmetic", count as doors in room descriptions, and number alongside the other locked doors. KOTOR 2, unlike KOTOR 1, uses the scenery flag on genuinely lockable doors; lock evidence now outranks the flag.
- Medical items work on KOTOR 2. Medkits, repair kits, stims and antidote kits fired from the keyboard did nothing — every press answered "Not possible" ("Nicht möglich"). KOTOR 2, unlike KOTOR 1, uses a two-step flow for the whole Medicine row: the press only marks the item, and a mouse click on a party member's portrait was the sole way to complete it, so from the keyboard the row was unreachable by design. The mod now sends the same use request that portrait click sends, so the row simply works again.
- Medical items can target the party. Selecting a Medicine item in the action menu opens a small picker with every party member by name, your current target preselected (else the character you control); Up and Down move with the names, Enter applies the item to whoever is focused, Escape backs out. Shift+Enter — on the item or inside the picker — applies it to yourself immediately, and the bare 5 key stays instant self-use with no picker. The player character's own row speaks the chosen character name and is targetable like any companion.
- Mines and other items that act without entering the action queue no longer announce "Not possible" while visibly working. The mod now watches the engine's own use-request channel: a request that went out counts as the press having acted, so the generic failure line only speaks when nothing happened at all.
- The KOTOR 2 workbench is fully navigable, including the crafting screens KOTOR 1 doesn't have. KOTOR 2 restructures the workbench: the first screen lists all your upgradeable items with category filter buttons (All / Lightsaber / Ranged / Melee / Armor), and new "Create Items" and lab-station screens craft and break down items. Arrow keys walk the item list and every button; Enter on a listed item opens its upgrade slots (or crafts / breaks it down on the crafting screens); Escape backs out through the engine's own Back button.
- The workbench crafting screens ("Create Items" at a workbench, medical crafting at a lab station) speak their title on entry, announce switching between the create view and the break-down view ("Create mode" / "Breakdown mode"), skip the list you can't currently act on, and keep navigation in step when a category button or a completed craft repopulates the list. Shift+Up/Down reads the focused item's full description blocks on the break-down list and the upgrade list.
- Upgrade-slot buttons on the KOTOR 2 upgrade screen announce their real slot type and contents again. The screen renumbers every control, so the slot labels ("Upgrade slot 2, empty" / "Crystal slot 1, fitted with …") and the Escape route to the Back button were still using KOTOR 1's numbering — slots spoke as unlabeled controls and Escape could hit a label instead of Back. KOTOR 2's six-slot lightsaber bank also gets crystal slots 5 and 6, which KOTOR 1 doesn't have.
- The workbench's unlabeled "Create Items" button (the game draws it icon-only) now speaks as the game's own "Create New Items" line in your installed language.
- Entering a workbench no longer speaks four screen titles in a row. Choosing the upgrade option in the workbench conversation makes the game open its whole screen stack at once — item list, item picker and the slot screen for a preselected item, with the slot screen in front — and every screen announced itself. Now only the screen actually in front speaks, and because the game reuses these screens, returning to one announces it again instead of staying silent.
- The upgrade slot screen tells you which item you are upgrading: it opens with "Upgrade: <item name>" instead of the meaningless placeholder "Item Name" it previously caught before the game had filled the title in. Escape from there lands on the item list ("Upgrade Workbench"), which re-announces itself.
- Opening the upgrade option at a workbench now puts you on your item list, not inside the first item. The game auto-opens the first upgradeable item's slot screen on top of the list; keyboard users landed there and had to press Escape to reach the list. The mod now sends you straight to the list, where you pick the item you actually want. Choosing an item still opens its slots as before.
- The upgrade slot's "no upgrade / remove" choice speaks a word instead of a bare number. When a mod slot's list of options held only the game's empty "-" entry, it was announced as "1" (or as silence on some screen readers); it now reads "No item" ("Kein Gegenstand"), and any option whose name can't be read falls back to the same word rather than a silent step.
- Installing an upgrade at the KOTOR 2 workbench works. Choosing a mod for a slot did nothing except drop you back to the item list — KOTOR 2 installs an upgrade the moment you pick it from the slot's list (and its "No item" entry removes whatever is fitted), where KOTOR 1 needed a separate confirm step. The mod was still doing KOTOR 1's two-step confirm, which on KOTOR 2 cancelled straight back to the list without fitting anything. Picking a mod now fits it, picking "No item" removes the current one, and you stay on the slot screen to keep working.
- Picking an upgrade for a slot that can't take it now says so instead of falsely confirming. When a slot is incompatible with the weapon, the game refuses the fit; the mod used to announce "Upgrade installed" regardless. It now checks whether the slot actually changed and says "Upgrade not possible" ("Aufwertung nicht möglich") when nothing was fitted, "Upgrade installed" only when it was, and "Upgrade removed" for a removal.
- A slot's mod list no longer reads two "No item" entries. A mod the mod can't name was being announced as the game's empty "No item" placeholder, so a slot could appear to hold two of them. Only the real "remove" entry now reads "No item"; an un-nameable upgrade reads "Upgrade" ("Aufwertung") so it stays distinct.

<h3>Speech:</h3>

- The mod speaks for people who do not use NVDA. If you use JAWS, Narrator, ZDSR, PC-Talker or no screen reader at all, it was silent from the moment it loaded. The mod picks a speech output by asking each supported reader in turn, highest priority first, whether it started up successfully, and takes the first that says yes. NVDA is asked first, and its support answered yes whether or not NVDA was running — the check for a running NVDA sat behind a query that itself only works when NVDA is running, so with NVDA absent the check was skipped. Everything was then handed to an output that refused all of it. The mod now also asks whether the reader is genuinely running, which is a live check that is answered correctly, and moves on to the next one when the answer is no.
- The speech library is updated to Prism 0.17.3, which fixes the same fault at its source. Both checks are kept, so speech no longer depends on either one of them being right on its own. The local fixes the shipped library carries were re-applied on top of the new version, including the one that keeps the game from crashing at startup when ZDSR, PC-Talker or BoyPC Reader is installed at a version whose files do not match.
- ZDSR braille output is supported where the installed ZDSR provides it, and a ZDSR too old to provide it loses only braille rather than failing to load at all.
- Braille displays show what the mod says. Tolk, the speech library the mod used before Prism, spoke and brailled in one call; the port to Prism only ever asked for speech, so braille displays showed nothing. Announcements now go through Prism's combined speech-and-braille call where the screen reader offers one, with a braille flash message alongside plain speech otherwise, and urgent lines spoken through the system voice are mirrored to your screen reader's braille display too.

<h3>KOTOR 1 — controller support:</h3>

- KOTOR 1 can be played with a controller, with the same layout KOTOR 2 has. The first game has no gamepad support of its own — its input layer only ever creates a keyboard and a mouse — so the mod reads the pad itself and, wherever the game rather than the mod has to act, presses your own bound key for you. Everything that made KOTOR 2 playable on a pad works here: the D-pad's object cycle, the action menu on the left trigger, the trigger chords for the beacon, autowalk, degrees and screen help, and one-entry-per-press navigation in menus.
- The left stick walks and strafes in eight directions and the right stick turns the camera, following whichever movement keys you have bound. The stick releases the moment you let go or the game loses focus.
- Y opens a quick menu: the menu screens, party leader, solo mode, stealth, quick save, and Help, which opens the mod's key list. KOTOR 2's own gamepad menu has one more entry, Switch Weapons, which the first game does not have. The left stick stops walking while the menu is open so you cannot wander off mid-choice.
- The remaining buttons do what they do on KOTOR 2: the shoulder buttons cycle targets in the world and step the in-game menu's sub-screens elsewhere, X switches party leader, Start pauses, the left stick press flourishes your weapon, and the right stick press turns the camera to the beacon's next waypoint.
- The key list's Controller section now appears on both games, and gains lines for X and Start. It is still shown only when a pad is connected.

<h3>World navigation:</h3>

- Rooms shaped like a horseshoe or a switchback ramp are no longer described as though they were one straight corridor. A corridor that bends back on itself was being merged end to end into a single space, and its exits were then named by their compass direction from the middle of that space — a point that, for a U-shaped room, lies inside the wall between the two arms. On the Peragus fuel-depot ramp this announced a door to the south-east that was 80 metres away around the bend and a storey up, with a wall in between; walking south-east was the one thing that could not work. Such a room is now split at its bends, so each straight run is described on its own and the direction you are given is one you can walk.
- Cycling to a container you have already emptied now tells you it is empty. The `,` and `.` object cycle left that note off while focusing the same container with Q or E included it, so whether you learned a footlocker had already been looted depended on which key you reached it with. Both now build the spoken name through the same code, so every part of it — the number, the door state, and the empty note — is identical on both.
- More exits speak their destination name. A door borrows its name from a map label placed within 5 metres (was 3); a survey of every area in both games showed KOTOR 2 places its labels behind the doorway rather than on it, so the old radius matched only one label in seven there. The wider radius roughly doubles the match rate in KOTOR 2 and helps KOTOR 1 too, while staying well inside the typical spacing between neighbouring doors.
- Doors in a corridor are announced on the end they are actually on. A corridor with an exit at each end kept only one door's name and put it in front of whichever direction happened to be read out first, so the name regularly landed on the wrong exit — and when both ends were doors, the second one's name was never spoken at all. Each end now carries its own name and destination: a passage that used to say "Door east-west" now says "Security door east, Door west".

<h3>Mod settings:</h3>

- Two new entries sit at the bottom of the Mod settings list: "Support the modder", which opens my Ko-fi page, and "Latest changes", which opens the release page of the newest version and the notes that come with it. Enter on either opens the address in your default browser.

<h3>Clipboard:</h3>

- Ctrl+R copies the last thing the mod spoke to the Windows clipboard. It was built for the computer terminals whose riddles ask you to do arithmetic on figures you have only heard, but it is not tied to dialogue: whatever was read out last — a journal entry, an NPC's name off the object cycle, a line on your character sheet — is what lands on the clipboard, ready to paste into a calculator or a notes file. The confirmation it speaks is never itself copied, so pressing the key twice gives you the same text both times. Rebindable, and listed in the key list under Mod features.

<h3>Input recovery:</h3>

- The keyboard now recovers on its own when the game stops answering it, instead of staying dead for the rest of the session. Switching to another window and back makes the game rebuild its own window, and the single attempt the mod made to wake the keyboard afterwards did not always land. The symptom was easy to misread: the mod kept speaking and its own keys kept working, because those are read separately, so everything sounded alive while nothing on screen would respond. The worst case was a confirmation popup — "Do you really want to quit?" — read out correctly, with both buttons where they should be, and impossible to press. The mod now spots the mismatch (you are holding a key the game knows, the game has heard nothing for a while) and re-wakes the keyboard until it answers.

<h3>KOTOR 2 — action menu:</h3>

- KOTOR 2's fifth action-bar column is reachable. The second game gives every character a combat-behaviour setting — Aggressive and three siblings — in a column the first game does not have, and the mod had no name and no key for it. It is now called Combat Behaviour, sits in the action menu alongside the others, and answers to 8 (apply the current setting) and Shift+8 (open it to pick another), both rebindable. Neither game binds anything to 8 on its own, so this key is the mod's rather than the game's; on KOTOR 1 nothing changes.
- Opening a category that happens to be empty no longer leaves you stuck. Shift+4 on a droid announced "Own Force Powers: empty" — correctly, a droid has none — and then refused to open at all, so no arrow key did anything and it read as a menu that had frozen. It now says which category was empty and opens on the first one that isn't. Only a character with no actions at all in any category still declines.
- An action that cannot be used now says so. Firing a medkit on a character already at full health used to give a blind player nothing to go on — KOTOR 1 played an error sound with an on-screen sentence you could not read, and KOTOR 2 did not even do that. The game's own reason is now spoken where it has one ("Full health"), and where it has none — KOTOR 2 accepts the action and then quietly does nothing with it — you at least hear "Not possible" instead of silence.
- Mod menus that are supposed to freeze the world now do so on KOTOR 2. The action menu, the examine view and the action queue all ask the game to pause when you open them, and on the second game that request was going nowhere: the engine function behind it had never been located there, so the request was made and silently dropped. The visible symptom was in the action menu with Action Menu auto-pause switched on — the world kept running, so firing an action closed the menu instead of leaving it open for the next one, and you could not queue several actions into one round the way you can on KOTOR 1.

<h3>KOTOR 2 — menu navigation:</h3>

- A screen's filter and toggle strip is read after the list it belongs to, not before it. KOTOR 2 places several of those strips above their list, and arrow navigation follows the screen layout, so they came first: the journal offered its three sort buttons before a single quest, and Load Game opened with the Cloud Saves toggle focused — which is also why that screen announced the first save slot and then a setting, and why one stray Enter on entry could flip that setting. On those screens the list now comes first and the strip follows the buttons below it.

<h3>KOTOR 2 — controller support:</h3>

- KOTOR 2 can be played with a controller. Everything the mod speaks for keyboard players it now speaks for pad players: in menus the D-pad and left stick move, A confirms and B goes back, all with the same one-entry-per-press announcements the keyboard gives; in the world the left stick walks, the right stick turns the camera, and the game's own Quick Menu on Y is read out. (KOTOR 2's Steam release ships gamepad support; KOTOR 1 has none, so this is a KOTOR 2 feature only. Plug the pad in before launching — the game only looks for controllers once, at startup.)
- In the world the D-pad steps through the objects around you: left and right for the previous and next object, up and down for the category — the pad's version of the `,` and `.` keys. A then acts on what you last heard, the way Enter does.
- The left trigger opens the action menu on whatever you have focused, and closes it again; B closes it too. Inside it the D-pad's left and right change category, up and down change entry, and A fires. It is the same menu the keyboard opens with Shift+Enter, so it obeys your Action Menu auto-pause setting — with that on, opening freezes the world and firing leaves the menu up, so you can queue several actions into one round before closing.
- The remaining bindings sit on the triggers and the right stick: right trigger alone announces your facing in degrees; left trigger with the left shoulder button drops an audio beacon on the focused object; right trigger with the right shoulder button walks you to it; both triggers together read out the keys for the screen you are on; and pressing the right stick turns the camera to the beacon's next waypoint, or to the next compass direction when no beacon is armed.
- The Quick Menu's "Help" entry now opens the mod's key list. It normally shows a picture of a controller with the buttons labelled, which tells a blind player nothing — so the entry now does what it promises. That list gains a Controller section, present only when a pad is connected, covering every binding above.
- Wheeled droids no longer grind on when you push them into a wall with the stick. The mod silences a stuck drive loop, but it was watching the movement keys, and a stick-driven walk holds no key.
- On the map screen the left stick pans the map cursor, at a speed that follows how far you push it. The D-pad reads the map's hints on the same axis it reads objects in the world — left and right for the previous and next hint, each spoken with its bearing and distance and with the cursor moved onto it.
- Containers and stores switch mode from the shoulder buttons, the pad's version of Q and E. Without them a pad player could only ever take from a container and only ever buy in a store.

<h3>Controller — status, action queue and tooltips:</h3>

- The left trigger with X reads your own status: health, active effects and equipped weapon, the same readout the keyboard's H gives, and it answers in menus as well as in the world.
- The right trigger with X opens the action queue, the keyboard's Shift+H. Inside it the D-pad steps the entries, A removes the one you are on, the left trigger with A clears the whole queue, and B closes. A removes an entry only while it is the last one queued for that character: the game has exactly one way to cancel a queued action and it takes the last one, so pulling an action out of the middle answers "Cannot remove this action" — clear the queue and re-issue what you still want. (That is the game's own limit, not the mod's: vanilla's cancel key, Y, does the same thing.)
- Holding Y in a menu turns the D-pad's up and down into the description readout — Shift plus an arrow on the keyboard — so an item's properties, a quest's log text or a power's effect can be heard one block at a time without leaving the entry you are on. Y is the modifier only in menus; in the world it stays the Quick Menu.

<h3>Installer:</h3>

- The installer now sets up KOTOR 2 completely, not just its community mods. Ticking KOTOR 2 installs the accessibility mod itself, the speech runtime, the loader, the audio cue files, the recommended graphics settings and the movement keybinds — the same set KOTOR 1 has always received. Previously it applied two engine patches and offered TSLRCM, then stopped.
- KOTOR 2's launch-time logo movies are skipped like KOTOR 1's. KOTOR 2 has no BioWare logo but adds an Obsidian and an Aspyr one, so the list is per game now.
- The movement keybinds (strafe on A and D, camera turn on Y and C) now apply to KOTOR 2 too. Its key table turned out to use the same entries as KOTOR 1, so the layout is identical in both games.
- Each game gets its own entry in Add/Remove Programs, and its own crash-dump capture. With both installed, a single shared entry could only ever point at one of them, which also made an installed KOTOR 2 invisible when the installer was run again later.
- The installer no longer starts while KOTOR 2 is running. Its "is the game running" check only recognised KOTOR 1, so a KOTOR 2 session could be left fighting the installer for the same files.
- Pressing F5 to update from inside KOTOR 2 now updates KOTOR 2. It previously updated the KOTOR 1 install and relaunched KOTOR 1, whichever game you pressed it in.
- The installer warns when Steam Workshop content is present for KOTOR 2. Workshop mods override anything installed into the game folder, so they quietly break TSLRCM and the Community Patch; you are asked to unsubscribe before continuing.
- If the installer cannot confirm that TSLRCM installed, it now asks you instead of assuming. The Community Patch and Tweak Pack must sit on top of TSLRCM, and a silent wrong assumption would have skipped both without saying so.
- KOTOR 2's mods install in the order the community's own build documents: TSLRCM, then the Tweak Pack, then the Community Patch.
- New optional mod on both mod screens: Thematic Companions, one checkbox per game. It gives each companion stats, feats and starting gear that fit their background instead of the values the games reuse from elsewhere. It is a balance mod rather than a bugfix, so it is the one box that starts unchecked — tick it per game if you want it, and start a new game afterwards, since it only affects a fresh character. It works on every language version: the mod writes no text at all, only creature stats.

<h3>Installer — KOTOR 2 in your own language:</h3>

- If you play KOTOR 2 in German, French, Italian, Spanish or Russian, the installer now installs the translated version of the Restored Content Mod instead of the English one, and your game's text stays in your language. Previously it installed the English version, which replaces the game's entire text file — so a German game became an English game, losing a translation that was already correct.
- The translated versions are only published on the Steam Workshop, so the installer opens the page for your language and you press Subscribe; it then waits for the download, copies the files into the game, and tells you when to unsubscribe again. Unsubscribing matters: files left subscribed are loaded separately by the game and clash with the other mods instead of combining with them. Once copied in, the community patches can patch them normally and add their own text to your language's file.
- One thing the translated versions leave out: corrected descriptions for four mine types, where the original game overstates their damage and difficulty. The mines behave exactly the same, and the descriptions are the same ones an unmodded game shows — the correction only exists in English, because it lives in the English text file.

<h3>Installer — KOTOR 2 fixes:</h3>

- The KOTOR 2 Community Patch now actually installs. It was being fetched from its GitHub repository, which holds only the instruction file and none of the 713 files the patch installs — those ship exclusively in its DeadlyStream archive. The result was an install that applied the patch's edits to existing game files, silently copied nothing, and reported success with a single warning. It now downloads the real archive, and refuses to run at all if the payload looks empty rather than half-installing again.
- After installing, the game now launches through Steam instead of directly, so it no longer complains that it must be started from Steam. The installer looks up Steam's own record of where each game lives, but was reading the 32-bit corner of the registry while Steam writes to the 64-bit one — so it never found either game and always fell back to launching the executable. This affected KOTOR 1 as well.
- The Steam Workshop page for the translated text can be reopened. Steam sometimes opens the wrong view, and closing that page previously left you waiting for a subscription you had no way to reach. The waiting screen now has its own button for it and explains that you need to press Subscribe on that page.
- The opening screen now lists both games and what state each one is in — installed with its version, present but unmodded, or not found. It previously described only one game, so with both installed the other was invisible.

<h3>Installer — still working years from now:</h3>

- When a mod can no longer be downloaded automatically, the installer now walks you through downloading it yourself and takes over again from there. It says what actually went wrong — a mod that has simply been updated since this installer was built reads differently from a failed download — opens the official page in your browser, and then offers the file in one press if it is already in your Downloads folder. Everything after you hand over the file is automatic, exactly as before. This matters because the alternative was a dead end: the installer verifies a fingerprint on files it downloads on its own, and once a mod updates, that fingerprint stops matching. A file you fetched from the official page yourself needs no such check.
- Mod version pins can now be refreshed without a new installer release. They moved into a small file the installer reads from the project's repository at startup, falling back to its built-in copy when offline. Keeping a years-old installer working no longer needs a new build.
- HoloPatcher, the tool that installs the community patches, now ships inside the installer instead of being downloaded. It was the one component with no manual alternative — you never see it, so you could not fetch it yourself — and if its download had ever gone away, no community patch would have installed for either game. Adds about 7 MB to the download.

<h2>v0.6.4</h2>

<h3>Polish translation:</h3>

- The mod now speaks Polish, and the installer runs in Polish. KOTOR's Polish release — the 2004 Licomp Empik Multimedia edition, subtitles only — was never sold on Steam or GoG, so Polish players lay it over a Steam or GoG copy; the mod recognises that and switches language on its own. Everything the mod says is translated: menus, navigation cues, help, the tutorial hints, effect names on the examine screen, and the combat callouts. Like French, Italian, Spanish and Russian, this is a machine-translated first pass that a native speaker will want to improve. The game's own dialogue and item names come from the Polish translation itself, as before.
- The Polish edition's own `swkotor.exe` works too, so it no longer matters whether a player copied it over their Steam or GoG one. It is the same 2004-03-05 build the Russian translation uses — byte-identical code, sixteen bytes of stored text apart — so the address handling already in this release covers both.
- Polish combat callouts needed more than a translation. The Polish game builds its attack lines in a different order, naming the target before saying whether the blow landed, where every other language reads "X hits Y". The mod now understands that shape instead of assuming the usual one. This is the one part not yet confirmed against a live Polish game; if a phrase turns out not to match, that line is read out in full rather than shortened.

<h3>Russian translation:</h3>

- Item, store and quest descriptions, galaxy-map navigation and closing a menu back to the world now work on the Russian translation (Allard 1.72). Twelve places in the mod still called into the game using addresses valid only for the standard build. On the Russian build the code is identical but the linker moved it, so those calls landed a few hundred bytes off, in the middle of an unrelated function — anything from nothing happening to a crash. They now go through the same address-translation the rest of the mod uses, and each one checks that its address resolved before calling, so a future gap degrades to a logged no-op instead of a crash. The standard game is unaffected: there the translation step is an identity.
- That same address set was re-verified while Polish was being added, and it holds: all twenty-five of the places the mod attaches itself to the game match on the Russian executable and on the Polish one alike. The two releases turn out to ship the same 2004 build, so one set of addresses now serves both.

<h3>Map hints:</h3>

- Dantooine's cryptic path markers now speak their destination. Vanilla reuses the same map-note strings for different transitions — "Southern Path" (Südlicher Pfad) marks the way to three different areas depending on the map, "Northern Path" (Nordpfad) three more, and "Exit" (Ausgang) four — so the spoken hint never said where the path actually leads. Those notes now announce the destination area's own name instead ("Matale Estate", "Courtyard", "Grove", ...), taken live from the game's string table so it self-localises in every game language. The two identical "Exit" notes inside the Sandral estate are now told apart as front exit and back exit. Speech-side only: no game file is modified, and mods that rewrite these notes keep their own text.
- The mod now ships its own curated map hints for story spots the game never marks: the unmarked backdoor of the Sandral estate (the feud-quest sneak entrance), the two dead rebels with the Promised-Land datapads in the Taris sewers, and the bantha herd's grazing ground near the krayt dragon cave. They appear in the map-hint cycle and under the map cursor like vanilla notes — hidden until the player has explored that part of the map — and support the beacon like a placed marker, but are never drawn on the visual map.

<h3>Bug fixes:</h3>

- French, Italian and Spanish games now speak the Endar Spire warning that the Republic soldiers cannot be saved and the way to the bridge is blocked. The line existed only in English, German and Russian, so on those three languages that moment passed in silence — leaving a blind player with no idea why walking toward the fight kept failing.
- Komad Fortuna's lines are read out in the Dune Sea. The Twi'lek hunter speaks an untranslated alien tongue, so his subtitles are your only channel, and the mod already knew to read him in Anchorhead and on Kashyyyk — but the krayt-dragon hunt itself is a third appearance that the game names differently again, so out in the dunes he fell silent. He is now on the read-aloud list there too.
- Griff Vao no longer has his lines read over his voice. Mission's brother is a Twi'lek, and an alien character model normally means "read the subtitle" — but like his sister he speaks fully voiced Basic, so the reading doubled his voice track. He joins the always-suppress list in both places he turns up in Anchorhead.

<h3>Character screen:</h3>

- The Feats and Force Powers tabs now speak the highest rank you have trained. Each row on those tabs is a chain — Power Attack I, II, III; Force Push, Whirlwind, Wave — and only its first entry was ever announced, so a fully trained power was named by its weakest form. The lower ranks are deliberately not browsable: once a higher rank is trained, the earlier ones can no longer be used.
- Force powers now also speak their cost — the base Force point price, the adjustment your alignment earns on light- or dark-side powers, and the resulting price per use. The game shows all three on that tab, but the mod had been suppressing them together with the Feats tab, where the same three fields are hidden and hold leftover text from whichever tab was open before.

<h3>Keyboard navigation:</h3>

- Home and End jump to the first and last entry on three more screens: the mod's settings menu, its audio glossary, and the feat and force-power grids in character creation and level-up. On the grids the jump crosses the whole screen — Home lands on the first ability, End on the Cancel button — rather than stopping at the ends of the current row, which are at most three cells wide and already reachable with Left and Right.
- The action buttons on the inventory and journal screens now sit at the end of the list and stay there. "Show:" in the inventory, and "Quest items", "Completed quests" and "Sort by name" in the journal, used to turn up in the middle of the list with more items after them — in the journal only once ten or more quests were active, which is why it looked inconsistent. Arrow navigation now walks every item or quest first, whatever the list length; press End to jump straight to the buttons.
- Cycling the inventory's "Show:" filter now gives you the filtered list. The game rebuilds the item list a frame later, which arrow navigation never noticed, so it kept the old number of rows and went on reading items the new filter had removed.
- Enter on an inventory item now uses that item. It used to use whichever item sat at the top of the list, so Enter on a medpac five rows down burned a charge off the shield at row one instead. The game's use handler reads the list's own selection rather than the row you activated, and a mouse click sets that selection on the way in where the keyboard never did.
- Shift+Down on the journal's quest-items screen now reads the description of the item you are on. It read the first item's description for every row: that screen resolved the description from the game's list selection, which arrow navigation does not move, instead of from the row you had arrowed to.
- The map cursor now follows only your primary movement keys — W, A, S and D on the default setup — which frees the arrow keys for menu navigation on that screen. KOTOR binds the arrows to movement as a second set alongside WASD, and the cursor had been honouring every bound movement key, so a single press of Down both panned the cursor south and stepped the menu at the same time. If you have rebound movement the cursor follows the new keys, as before.
- Four buttons that duplicate something the keyboard already does are gone from arrow navigation. On the map screen, "Previous map note" and "Next map note" — the map cursor sweeps those same notes with W, A, S and D — so Up and Down now step straight to the party-select and return buttons. On the party-selection screen, "Add", because Enter on a companion's portrait already adds or removes them. In the inventory, "Use": Enter on the item does the same thing and is safer, because the button acts on whichever row the game still considers current, which is not always the one you are standing on.

<h3>Under the hood:</h3>

- A large internal cleanup runs through this release. The mod's code was broken up into smaller, self-contained modules and its shared machinery — logging, speech, menu navigation, the way it reads the game's memory — was consolidated so each job is done in one place instead of several. None of this changes what the mod does; it is groundwork, and it is why this release carries so many commits for so few visible changes.
- The same work separates what is specific to the first game from what is not, in preparation for the KOTOR II port. Everything tied to KOTOR 1's executable now sits behind a single boundary, so the second game can supply its own layout without the rest of the mod being rewritten around it.
- Project pages in Polish and Russian. The README that describes the mod, its keys and its settings is now available in all six languages the mod speaks.

<h2>v0.6.3</h2>

<h3>Russian translation:</h3>

- The mod now runs on the Russian translation (Allard 1.72). That translation replaces the game's own program file with its own build, which the mod previously refused to install onto — so Russian players could not use it at all. The mod now recognises that build and works on it: every address it uses inside the game had to be re-found there, because the code is the same but the linker put all of it in different places. Menus, navigation, combat, character creation and the map all behave as they do on the standard game.
- The mod speaks Russian. It detects a Russian installation by inspecting the game's text file rather than trusting what that file declares — the translation identifies itself as English — and switches both its own announcements and the game's text to Russian. **The Russian announcements are a machine-translated first draft and have not been checked by a native speaker; corrections are very welcome.** The other translations added this way (French, Italian, Spanish) carry the same caveat.
- Installing a game translation *after* the mod no longer leaves the mod misbehaving. The patch framework remembered which version of the game it had originally been installed onto and kept trusting that memory even once the program file had been replaced, so it would drive the new build with the old build's addresses. It now notices that the program file is no longer the one it recorded. This affects any translation or re-pack that replaces the game's program file, not only the Russian one.
- If you already installed a translation on top of the mod and saw odd behaviour, reinstalling the mod now sorts it out.

<h3>Bug fixes:</h3>

- ZDSR users now hear the mod through ZDSR itself instead of a Windows voice. The speech library the mod uses asked ZDSR's client library for its functions under names no ZDSR release has ever published, so the connection could never succeed — at first that crashed the game at startup, and once the crash was contained it degraded quietly to a system voice, ignoring your screen reader's own voice, speed and language. The lookup now uses the published names, and the client library is found next to the mod or through ZDSR's own installation entry when it is not already on the system path. The same fault blocked BoyPC Reader and PC-Talker, which are fixed with it. It only ever affected 32-bit games — KOTOR is one — which is why it went unnoticed in the speech library itself; the fix has been offered upstream so other games benefit too. This one still needs confirmation from an affected user, since it cannot be tested without those screen readers installed.

- The Key Mapping screen is navigable again — every binding in a category can be reached and heard, instead of just two. Arrowing through a category only ever spoke the two entries either side of one fixed position, and that middle entry was never spoken at all: the game re-selects a row in that list on its own between keypresses, so the mod's keyboard position was discarded each time and every step was computed from the game's row rather than yours. The mod now keeps its own position in the list and asserts it before each step. Pressing Enter also binds the entry you actually navigated to — with the position hijacked it could otherwise have armed a rebind on an entry you never visited.

<h2>v0.6.2</h2>

<h3>Bug fixes:</h3>

- Opening the Level Up screen (Shift+L) now speaks a short instruction instead of a leftover placeholder. The game's own level-up description box carries an unfinished developer label ("Items Available to Place in Container and blah blah blah") that no language ever fills in, and the mod read it aloud on entry. In its place you now hear a brief how-to — choose a category with the arrow keys, press Enter to open it, spend your points, then Accept — so the moment guides you instead of confusing you. Spoken in all five languages. (Character creation already suppressed the same leftover.)

- The camera no longer spins endlessly after a conversation. Once a dialogue with a reply list closed, the view could start turning on its own and never stop, with nothing short of restarting the game to end it. The cause was two mod features colliding: to stop the game snapping your reply highlight to whatever the mouse hovers, the mod parks the cursor in the top-left corner while a conversation is open — but it parked it close enough to the screen edge that the game's own edge-scroll camera turn took over the moment you had control of the world again, and the mod's guard against exactly that spin was set one pixel too tight to notice. The cursor now parks further in, and the guard's detection margin is wider, so neither this park nor a future one can slip past it.

<h3>Endar Spire tutorial:</h3>

- The Endar Spire opening can be finished again. In v0.6.1 the mod answered your open attempt on the door beside the doomed-soldier battle with "Sealed. It will not open." and stopped there, on the assumption that the door was a permanently script-locked cutscene barrier. It is not sealed at all: opening it is precisely what starts the scripted battle, brings in the Sith reinforcements you then fight, and — once that room is clear — unlocks the door onward to the bridge. Because the attempt never reached the game, none of that ran, and the tutorial stalled with a locked door and nothing left to fight. Open attempts now always reach the game.
- The door where Trask sacrifices himself is no longer misreported as a permanent dead end. It stays locked — that is correct, you leave the Command Module through the Starboard Deck — but trying to open it is exactly what triggers his scene: the game's script stops you to level up first, then, once you have, plays his sacrifice and removes him from the party. The mod had been intercepting that door with a "sealed, will not open" line, which both suppressed the trigger and told you to give up on the one door you must push against to advance. It now reaches the game's own handling again. (The earlier assumption that this was a dead "Test Door" leftover was wrong: its fail-to-open script is the Trask-death sequence.)
- Once Trask has sacrificed himself, his door now says so instead of repeating a bare "locked". After his scene he is on the far side of it and it never opens again — the way on is the Starboard Deck — but the game only reported it as locked, which reads like something you still have to solve. It now speaks the same "sealed, will not open" line as the other dead door. Before his scene the door stays silent on purpose: at that point the failed open is exactly what triggers him to make you level up, and telling you to give up there would be wrong.
- Locked-door explanations are now attached to the game's own "locked" message instead of standing in for your open attempt. Both stalls above came from the same shortcut: the mod recognised a door by name, spoke a helpful line, and skipped the game's handling — but in this engine the open attempt is often itself the story trigger, so skipping it silently removed a step from the plot. The explanations are unchanged and still speak; they simply follow the game's report that a door refused to open. A door the mod misjudges can now cost you one unnecessary sentence, never a stuck game.

<h2>v0.6.1</h2>

<h3>Installer:</h3>

- The bundled patch framework is updated to Kotor Patch Manager 0.6.0 — a rebuilt patcher DLL (carrying our wrapper fixes) and the refreshed 0.6.0 address database.

<h3>Action menu:</h3>

- Out of combat you can now queue several actions from the action menu in one pass. Pause the game first (the pause key), and each action you pick is queued while the menu stays open on your selection, so you can line up the next one without reopening. Escape then closes the menu, resumes the game, and runs the queue — the same way the game's own menus unpause on Escape. With the game running, picking an action fires it and closes the menu, so a single action stays one keypress.

<h3>Bug fixes:</h3>

- The Level Up screen (Shift+L) now opens ready to navigate. Before, the wizard appeared but the keyboard was dead until you pressed Escape once to jolt it awake — opening the screen directly left the engine in world-input mode, so its keys were routed to the world and coded as world commands, and the menu never received navigable input. It now switches into menu-input mode and pauses the world the way a normal in-game screen does, so its options navigate on the very first key press and your character no longer walks around behind the open menu. Closing the screen restores world input and unpauses.
- Neutral creatures no longer repeat their name over and over after a fight ends. When combat finishes, the game's target focus can flicker on and off a nearby bystander (a Gizka standing near the battle, say), and the mod was re-announcing it every few seconds. An auto-focused creature is now announced once and stays quiet while the focus keeps flickering back to it; cycling targets yourself with the hostile-cycle keys still re-speaks on demand.
- Droid and computer terminal reply menus can reach every option again when a widescreen or high-resolution patch is installed. These interfaces have no native keyboard reply navigation — the game ties the highlighted reply to the mouse position, so the mod moves the highlight itself — but the game re-applies that mouse-driven highlight whenever the cursor sits over a reply row. On a 4:3 screen the resting cursor sits in empty space, so this never showed; a widescreen layout can leave the cursor hovering a reply, and the game then kept snapping the highlight back to that row so the keyboard could never rest on the options above it. Most visibly, a droid's repair submenu lost its manual-repair path and its Back option — both went unreachable and unspoken. The mod now parks the cursor clear of the reply list while a conversation is open, so keyboard navigation reaches and speaks every reply regardless of screen resolution.

<h3>Endar Spire tutorial:</h3>

- The doomed-soldier scene now flags the Sith side and every fighter, not just the first Republic soldier. The opening's un-winnable firefight was already announced as a moment to pass rather than join, but only the first Republic soldier you focused triggered it — the Sith cutting them down still read as ordinary enemies, and focusing the other soldiers said nothing. Focusing any fighter on either side now speaks the "they are lost, hurry to the bridge" cue, once per fighter.
- The cutscene barrier door beside that battle now announces itself as sealed instead of giving a bare "locked". The scene's script holds it shut with no key and it never opens, but the game only barked the generic "This object is locked" — reading like a lock you could pick. It now speaks an explicit "sealed, will not open" line.
- A locked door on the way to the bridge no longer strands you with no explanation. That door stays sealed until the nearby fight is cleared; reaching it early — or arriving after the scripted battle failed to start — previously gave only "locked", and players poked it and gave up. Trying it now says the door opens once the area's battle is over, and after several attempts adds that the tutorial may be stuck and to load an earlier save. The opening's plot-progress state is also written to the log so a stuck report can be pinpointed.

<h2>v0.6.0</h2>

<h3>Stealth:</h3>

- While Stealth mode is on and a hostile creature is focused, the distance to
  it is now spoken as a bare metre count as it changes, so you can hear an
  enemy closing and know when you are inside the ten-metre range where a strike
  from stealth lands the Sneak Attack bonus. Silent when not stealthed or with
  no hostile focused.
- Carth's stealth tutorial on Taris now teaches the keyboard control. His
  mouse-worded "click the Stealth ability to activate it" line fires a popup
  telling you to press G to turn stealth mode on or off (German and English;
  other languages keep the original line for now).

<h3>Controls:</h3>

- New installs now default to an ergonomic movement layout for blind play:
  strafe on A and D, and turn (camera and walking direction together) on the
  bottom-left key and C — Y and C on a German keyboard, Z and C on a US
  keyboard. W and S still walk forward and back. The installer writes this into
  swkotor.ini on a full install only; updating an existing install leaves your
  bindings alone. The swoop and turret minigames keep A/D steering.
- The movement tutorials now teach all six keys and explain that turning
  rotates the camera and your walking direction together, so the two always
  point the same way.

<h3>Localisation:</h3>

- The tutorial popups are now translated into Italian, Spanish and French,
  matching the existing German and English. This covers the whole set — the
  rewritten silent game tutorials and our own Endar Spire popups (Trask's
  guided-conversation hints, the level-up hint and the stealth hint). These
  three languages previously fell back to the vanilla mouse-worded text.

<h3>Action menu:</h3>

- Actions fired from the menu now always hit the focused target. During an
  active fight the engine kept re-aiming its internal action lists at your
  current combat opponent for as long as the menu sat open, so a Force power
  picked for a cycled-to object could silently fire at the enemy you were
  already fighting — a Force Breach meant for a captive Jedi landed on Malak
  instead. The intended target is now restamped into the engine's lists at the
  moment of firing, closing the window entirely.
- The open menu now follows target cycling. Cycling with comma/period or Q/E
  while the menu is open re-anchors it to the new target: the rows rebuild to
  that target's real options, and your selected action carries over by
  identity — so cycle, Enter, cycle, Enter casts the same power on one target
  after another, each cast on the right object. If the new target doesn't
  offer your selected action, that Enter announces the new target's menu
  instead of firing something you haven't heard; the next Enter fires.
- A menu opened without a target grows the target rows mid-session. Cycling
  onto an enemy or door while a personal-only menu is open folds its
  attack/power/item rows in, exactly as if it had been focused at open time;
  targets with no actions leave the menu personal-only.
- Opening a target column that turns out empty no longer leaves the previous
  menu armed. "Column N is empty" used to keep the earlier menu — built for
  an earlier target — silently navigable underneath, so firing from it hit
  that old target. The refusal now closes the stale menu outright.
- Out of combat, firing an action now closes the menu and hands control back
  to the world. The menu used to stay open and paused after every Enter so you
  could stack several actions into the engine's queue — a combat affordance —
  but out of combat you almost always want one action and an immediate return,
  so the lingering paused surface only added an Esc/unpause step the sighted
  mouse radial never charges. Out of combat, Enter now fires and closes, matching
  that radial; in combat the menu still stays open so you can queue several
  actions and run them with Esc.

<h3>World interaction:</h3>

- Shift+R is a second key for the force-radial action menu, alongside
  Shift+Enter. It is an independent, separately rebindable binding — both
  open the same menu, so whichever is easier to reach works.
- Pressing R on your current target now speaks the default action it takes —
  open, talk, attack, and so on. R has always performed the game's default
  action on the focused object; the mod now announces which action that is as
  it fires.

<h3>World navigation:</h3>

- Landmark announcements now trigger from the same distance at which sighted
  players discover them. The game reveals map notes through its fog-of-war grid,
  whose cell size scales with the map: about 6-8 m in interiors but 27-48 m on
  open maps like the Dune Sea or the Dantooine plains. Our fixed 8 m range meant
  you could run straight past landmarks that had long appeared on a sighted
  player's map. The range is now derived per area from that fog grid (1.5x the
  cell size, never below the old 8 m), so open-world landmarks announce from
  realistic distances while interiors behave as before.
- The blocked end of a corridor now reads as a dead end instead of the
  corridor's through-direction. Standing at the walled-off west end of an
  east-west hallway, the room description spoke "east-west" — the corridor's
  orientation — which reads as though the passage keeps going west, sending you
  searching for an exit that isn't there. The test that decides whether a dead
  end is a real spot you can walk into recognized only tight alcoves, not the
  wider end of a corridor, so it threw away the correct "dead end, exit east"
  label and fell back to the adjacent corridor's axis. It now also recognizes a
  corridor terminus — an open way back, a wall close behind, and corridor-width
  walls to either side — so the end of the hall announces as a dead end and
  points you back the way you came.
- Cosmetic (set-dressing) doors are no longer named as exits in room
  descriptions. A sealed decorative door on a corridor wall could be folded into
  the passage's shape label as a door you could pass through, so a hall that
  actually ends there read as a way onward. Being non-interactive, these doors
  are now left out of the room-shape door labels entirely, so a corridor is
  described by its walkable geometry rather than a door you can't use.

<h3>Late-game finale:</h3>

- The Shift+number action menus now always match your current target. Opening a target's attack or Force-power column with Shift+1 to Shift+3 read whatever target the engine's menu had last been built for, so on a freshly focused target it often announced an empty column until a bare number key had been pressed once — most noticeable on story objects you must target mid-fight. The consumed Shift press now rebuilds the menu against the narrated target exactly like the bare number keys do.
- Story objects in the finale now speak their state the way doors speak open and closed: the captives in the last arena read "captive" or "freed", and the droid-control terminals on the decks before it read "active" or "deactivated". The state is spoken on every surface — Q/E targeting, passive focus, and comma/period cycling — so a used-up object that offers no more actions is self-explanatory the moment you hear its name.
- The fight-critical terminals and captives of the last levels are discovered automatically on entering. Finding a never-visited console for the first time while under fire is close to impossible without sight, so they are placed into discovered-object cycling the moment the level loads — no prior exploration walk needed.

<h3>Floor-plate puzzle:</h3>

- Press R at any time in the puzzle room to hear the whole board — which plates are lit, and how many of nine. Until now you had to carry the running state in your head from the step-by-step announcements alone.
- Stepping on a plate now names what changed rather than the resulting state — "north-west lights up, west goes dark" instead of "north-west lit, west dark" — so a toggle reads as an event, with the lit tally still following.
- View mode (B) drives the puzzle's distance and entry announcements. With the character frozen you can sweep the virtual cursor across the grid and hear each plate's compass offset, and its name as the cursor crosses onto it — a way to survey the board without stepping, and without triggering any state change.
- The room's introduction is now spoken urgently so it is not cut off as you walk in, names the key that reads the board, and reminds you that a story clue points to the solution.

<h3>Combat speech:</h3>

- Blaster deflection no longer spams raw dice math. Every deflected shot used to speak the game's full "Deflection Breakdown: … roll 12 + Jedi Defense 6 + …" line — over three hundred such lines in one logged session. Deflections now collapse into one short line per volley, "X deflects 3 shots", spoken for party members only.
- Shield and damage-resistance absorbs merge into one spoken total. The engine's odd colon phrasing of the resistance line ("name : damage resistance absorbs …") is normalized to the plain shape, both absorb sources feed one running total, and the collection window is widened so an autofire volley speaks once instead of once per pellet.

<h3>Status effects:</h3>

- Effects now speak the same names sighted players see as icons. The buff/debuff icon row on the portrait is read directly, so instead of the generic type words "Immunity" and "State" you hear the actual source — "Force Immunity", "Energy Shield", "Sith Energy Shield", "Stun", "Master Valor", "Adrenaline Shot: Strength" — using the game's own names in all five supported languages, in icon order. Script-applied buffs that display no icon fall back to the previous generic type names, so nothing that was audible before is lost.

<h3>Tutorial:</h3>

- The journal, equipment and inventory tutorial pop-ups now explain how to read full details. As you arrow through these screens you hear only the focused entry's title or item name; the pop-ups now add that Shift and an arrow key reads the whole thing — the quest entry's text in the journal, an item's description in equipment and inventory.
- The inventory (party stash) tutorial pop-up is now covered like the others. It wasn't among the mapped tutorial screens, so opening the inventory read out its original mouse-era wording; it now speaks a keyboard-oriented hint instead.

<h3>Endar Spire opening:</h3>

- Interacting with the sealed "Test Door" in the Endar Spire opening room now
  says "Sealed. It will not open." instead of announcing an Open action that
  does nothing. This is a leftover, permanently locked door BioWare never gave
  a story line; the engine offered a misleading Open prompt and then fell
  silent, which is confusing in the first minutes of the game. Only this one
  door is affected — the neighbouring locked door still gives its Trask "make
  your way to the Starboard Deck" line.
- Trask's level-up reminder at the Endar Spire's locked door now speaks on its
  own. His line telling you that you have enough experience and should level up
  before going through the door ships as subtitle text with no recorded voice,
  and because Trask counts as a voiced human the mod's human-subtitle
  suppression silenced it entirely — you only got it by pressing R. It is now
  force-spoken in every supported language, queued after the door's locked cue
  rather than over it, and followed by a one-off reminder that R repeats the
  last spoken line.
- The Endar Spire's doomed-soldier scene now announces itself as a moment to pass, not join — one of the tutorial's main confusion points. In the command module you come upon Republic soldiers making a last stand against the Sith across a walkmesh gap you cannot cross; sighted players watch the firefight and move on, but the mod previously gave only the soldier's name, and a walk toward him ended in a generic "way blocked" — reading as an ally you inexplicably couldn't reach. Focusing one of these soldiers now speaks an in-world line that they are lost, you cannot help them, and must hurry to the bridge, and every attempt to walk to one repeats it.

<h3>Bug fixes:</h3>

- A story-locked door or container now repeats its explanation on every attempt, not just the first. These objects say why they won't open — "Security lock, access code required" and the like — through a one-time bark that the game fires only on your first try; every later attempt spoke just the generic "This object is locked", so if you missed that first bark the reason was gone for good. The mod now remembers each locked object's explanation and replays it after the generic line each time you try the object again.
- Droid and computer terminal menus read their reply choices correctly again
  when an interface mod is installed. These panels carry a second, hidden list
  — the scrolling terminal output — alongside the reply choices, and the mod
  located the choice list by scanning for the first list in the panel. That
  works on the unmodified game, but a widescreen/HD interface mod that renumbers
  the panel's controls could make the scan latch onto the terminal-output list
  instead, leaving the reply options unreadable or stuck on a single entry. The
  reply list is now addressed by its fixed position in the dialog structure,
  which no interface mod can move.
- Menu text that always spoke German on non-German installs now follows the game's language. Three spoken strings bypassed the localization table and were hardcoded to German: options-screen toggle states (now spoken as "on"/"off" in your language rather than "ein"/"aus"), slider value readouts ("8 of 10" rather than "8 von 10"), and the name of the Equipment sub-screen — the one main-menu screen whose caption the engine never exposes through its string table, so it fell back to a literal "Ausrüstung". All three now resolve through the localized table in every supported language (English, German, French, Italian, Spanish).
- The character-creation portrait selector now works the moment it opens: it announces the current portrait immediately, and Left/Right speaks each newly-selected portrait as you cycle. The panel is pushed by its parent menu without an engine focus event, so keyboard navigation stayed bound to the parent until your first arrow press — leaving the panel silent on open and making Left/Right fall through to a bare click with no readout. You previously had to press Down then Up to wake it up. Navigation now binds to the portrait panel as soon as it appears.
- The quest journal now reaches its entry when only one quest is active. With a single active quest — the norm early on, where the Endar Spire's "Attack on the Endar Spire" is your only entry — the quest list was dropped from keyboard navigation, so opening the journal and arrowing through it reached only the footer buttons and never the quest; a "new journal entry" notice appeared to lead nowhere. The lone entry is now navigable and reads its full text on Enter, exactly as it already did with two or more quests.
- The F1 help now describes the two object-cycling key pairs accurately. Q/E was labelled "cycle nearby targets" and comma/period "cycle objects in the current category", which wrongly implied one was for enemies and the other for objects — both cycle objects. The lines now draw the real distinction: Q/E steps through what is currently in view, comma/period through the objects you have already discovered.
- The turret minigame's "select next target" key works again, and saved keybinds are stored under their correct names. An internal keybind name-table was missing two entries, which shifted every later name and left this action mislabelled and bound to R instead of E; it also wrote malformed lines — including a blank "(null)" entry — into the saved-keybinds file. The table is corrected and bindings now persist under their proper names.
- Objects no longer go unfindable after a scripted cutscene. Just past the Endar Spire's opening dialogue, a door you were standing right in front of couldn't be reached by Q/E or comma/period cycling and wasn't announced as you approached — only wide-area object cycling could find it. The opening holds a screen fade at full opacity for a stretch after it visually ends, and the engine skips its per-frame scan of nearby targets whenever the screen is obscured — reasonable when a sighted player is staring at black, except that same scan is what rebuilds the Q/E target list and drives on-approach narration, so both stayed frozen until the fade released. The mod now runs that scan itself while a held fade is the only thing suppressing it during normal play, keeping targeting and narration live.

<h2>v0.5.9</h2>

<h3>The last blocker:</h3>

- A certain late-game floor puzzle — until now the last known blocker for finishing the game without sight — is fully solvable by ear, by skill rather than automation. Without spoiling anything: the room introduces itself as you approach, every step onto it answers with exactly what changed and a running tally, a marked spot lets you start over cleanly, and once you beat it the room goes quiet for good. Directions inside it come as single compass words with step-sized distances, matched to how the room is actually walked. Companions can interfere with this puzzle, so if they're with you the mod recommends solo mode and speaks whatever key you currently have bound to it. With this, the game is playable start to finish without sight.
- The puzzle's floor triggers no longer fire the area-exit proximity sound or clutter the transition cycling category; the puzzle's own announcements replace both.

<h3>Tutorial:</h3>

- The whole in-game tutorial now teaches the keyboard instead of the mouse. Every one of the game's built-in tutorial pop-ups used to explain actions by clicking, dragging and the mouse wheel — no help without sight — so each now speaks the matching keyboard command. The opening movement pop-up points you to the F1 help list and Ctrl+F1 for the current screen's keys before giving the walk keys; the map pop-up covers moving the map cursor, cycling map hints, dropping your own, and starting a beacon or an auto-walk to one; and the action-menu, targeting, equipment, combat and bash pop-ups each speak their keys, including the number and Shift+number action-menu shortcuts. The on-screen text is left as the original mouse wording, so a sighted or low-vision player using a mouse still reads it — only the spoken line changes, and you can arrow onto the message to hear it again.
- On the Endar Spire, Trask's spoken tutorial now raises its own keyboard pop-up at each step. His guided walkthrough coaches the mouse — click your gear, aim the camera at the footlocker, click a portrait to change leader — so as each of his lines finishes the mod pauses the game with a pop-up that gives the keys instead: opening and browsing your equipment, finding and walking to the footlocker, switching who you control, opening screens, using the action menu and medkits, opening doors, slicing security, and levelling up. Where two of his lines carry the same advice, the pop-up speaks it once.
- Trask's footlocker step now teaches the direct gesture — cycle to the footlocker with Q and E and press Enter to walk over and open it. It previously pointed at the discovered-object keys (comma/period, then Shift+minus to walk), which are for revisiting things you have already found rather than acting on the object right in front of you.
- More of the game's built-in tutorial pop-ups now teach the keyboard. The "new journal entry" notice now points you to the quest screen and its key (L); the messages-screen notice does the same for J; and the "party member has fallen" notice keeps its revival explanation and adds that Tab switches who you control. These three used to speak only the original mouse-era wording.
- The targeting pop-up keeps its combat rule. It now ends with "in combat you can only target enemies" after the Q/E and object-cycling keys, instead of dropping that clause.
- Fixed a double-read on tutorial pop-ups. On the targeting and equipment pop-ups the mod spoke the correct keyboard hint and then, a moment later, read the original mouse wording again over it — the per-tick focus monitor only knew about Trask's pop-ups, not the game's own. Now you can arrow onto the pop-up's message and hear only the keyboard hint.
- Trask's follow-up questions now raise their pop-up too. Asking him again about opening doors, using a medkit or improving your character used to end and return you to the world with no keyboard pop-up, because the recap only fired at a reply break the branch never reached. It now also fires when the conversation closes.
- The opening movement pop-up now also mentions the exploration cues: tones warn you as you near a wall, the shape of the room is described as you move, and the right Alt key repeats your facing direction and current room.

<h3>Party screen:</h3>

- The party-select screen no longer calls Trask "Bastila Shan" during the Endar Spire tutorial. Trask occupies the roster slot the game reserves for Bastila, and when the screen couldn't look up a live name for that slot it fell back to a fixed companion list and announced the wrong name for a portrait that is actually Trask. It now reads the character actually occupying the slot, so it identifies him as Trask.

<h3>World navigation:</h3>

- Walk-through area exits are now announced by name. Outdoor maps use invisible walk-through zones instead of doors for their area transitions, and the room descriptions treated the walkmesh simply ending there as a dead end — on the Unknown World's south beach the map's only onward exit spoke as "dead end" for an entire session. Such exits now appear in room descriptions as a named transition ("transition east to Temple Exterior"), and a corridor holding one is no longer called a dead end.
- Tiny path fragments no longer chatter as you pass through them. Organic outdoor walkmesh (the Unknown World beaches) splinters into regions only a few metres across; each used to announce a bare compass list the moment you clipped it — over a hundred such announcements in one logged half hour, while the map's one meaningful label spoke three times. Regions under twelve metres now announce only after you have actually stayed in them for about two seconds. Passing through is silent and keeps the previous region current, so stepping briefly into a fragment no longer re-announces the big neighbour on your return; stop anywhere and the local label still arrives.
- Diagonal corridors now name both ends. A straight north-east-to-south-west passage used to abbreviate to just "north-east" — by ear indistinguishable from a single exit pointing north-east, which hid the south-west continuation entirely (on the south beach that hidden end was the only route to the beach). They now read "north-east, south-west"; the cardinal short forms ("east-west", "north-south") stay, since those can't be misheard as a single direction.
- Detected mines and traps are announced with direction and distance — at the same moment a sighted player sees the red ground overlay appear, never earlier. The game's own detection message ("Jolee detected Frag Mine: Awareness 31 vs. DC 30") now speaks with a clock direction and distance in place of the dice math; trapped doors and containers, for which the game prints no message at all, get their own "Trap detected" announcement; and coming within four metres of a detected mine speaks one warning — no repeats while you disarm it or step over it deliberately, re-arming only once you walk away so a minefield warns again on the way back. All of it mirrors the game's own per-trap detection state, so nothing is revealed early.
- Sealed decorative doors now read as "cosmetic" instead of "locked". Some doors in the world are pure set dressing — the game never lets anyone open them, and no key or security check exists for them. They were announced as "locked door", which sent you hunting for a way through that isn't there (the Manaan Sith base has three of them). They now say "cosmetic door" so you can tell them apart from a genuinely locked door and move on. Because the label changed, they also stop sharing the "locked door 1/2/3…" numbering with real locked doors, so the count of actual locked doors in an area is no longer inflated by scenery.
- Standing at an undiscovered map-hint location now discovers it. Some map hints — nearly half of them on the Unknown World, rare elsewhere — start hidden and are only revealed when you cross an invisible, often metre-thin script trigger placed where a sighted player would naturally walk. Navigating by walls and audio cues slips past these strips: in a logged session the temple-entrance hint stayed unrevealed through six passes and a visit to within ten metres of the spot, and only appeared half an hour later when a party member happened to cross the strip mid-fight. Now, when your party leader comes within five metres of a hidden hint's actual location, the mod reveals it the same way the game's own trigger would — it announces, enters map cycling, and can be routed to immediately.
- Opening a door now speaks the direction you end up facing. The game turns your character toward whatever you interact with — and often walks you there first — so it's easy to lose track of which way you're now pointing, and where you came from. When you open a door the mod announces your facing (e.g. "East"), just the direction and nothing else. If that same direction was already announced within the last second — as happens when the game spins you toward a door right next to you — it stays silent so you don't hear it twice; a door you reached after walking and turning gets the announcement. Only doors you open yourself trigger it, not ones a companion, an enemy, or a scripted event opens, and it fires as the door starts to open rather than after.

<h3>Bug fixes:</h3>

- Story and script triggers no longer masquerade as area transitions. A broken emptiness test classified every trigger without a destination — traps, conversation triggers, shield walls — as a transition, so they appeared in transition cycling and played the transition proximity sound (a conversation trigger sounding like a doorway was pure confusion). Only triggers that actually lead somewhere count now.
- The "free directions" stuck announcement waits a full five seconds. It fired after two seconds of no progress (or four of circling), which interrupted deliberate tight manoeuvring along walls; both detectors now require five seconds of genuinely going nowhere.
- Map-hint landmarks and area transitions now read their real name when you cycle or route to one in the world, instead of the internal id (e.g. "k35_map_dreshdae"). World cycling and beacon/auto-walk routing read the object's name field — a resref-style tag in stock KOTOR — rather than the map-note label the map screen already used; they now use that label too.
- Lightsaber crystal stats read correctly again at the workbench. When item tooltips were grouped into categorised blocks (Tags/Values/Properties/Description) you step through with Shift+arrow, crystals lost their numbers: a crystal's stats don't divide into those blocks, so it fell back to the description alone — which for a crystal is lore text with no attack or damage figures. Browsing crystals in a saber's upgrade picker now speaks the game's own computed bonuses again, the same text the workbench shows on screen.
- Saving no longer starts an unwanted conversation. When you confirmed a save-game name with Enter while an NPC was your current target, that same Enter also registered as an in-world interact and quietly queued a "talk to" action; with the world paused it waited until you left the menu, then walked you over and opened dialogue the instant play resumed. The naming box now consumes its confirm Enter so saving leaves your target alone.
- Rebinding the camera-turn keys away from A/D no longer breaks the mod's orientation features. The spoken direction as you turn, the N hotkey (face the next compass point, or the current beacon), the map cursor, and auto-walk's cancel-when-you-move all assumed the default A/D turn and WASD movement keys — so after you changed those bindings in the game's Keyboard Mapping they watched the wrong keys, and the N hotkey stopped turning the camera entirely. They now read whichever keys you have bound to turning and moving. The turn announcement also switched to following the camera's actual rotation rather than a specific key, so it works the same whether you turn with the keyboard or the mouse. (QWERTZ note: turn keys on the Y/Z row are now resolved by physical position, so a turn bound to Z drives correctly.)
- Map-hint landmarks the game reveals mid-visit are picked up now. Some hints only become active while you are already inside the area — the renegade-Sith hint in the Shyrack cave, for example — but the mod scanned for hints once on entering, so a late-enabled hint never announced on approach, never entered discovered-object cycling, and only the Extended-cycling override could surface it. The mod now notices newly enabled hints within a second, and they announce, discover and route like any other landmark.
- Rooms you fought your way into get described once the fight ends. Room-shape announcements are rightly held back during combat, but the held-back announcement was dropped for good — leaving you in an undescribed room exactly when a fight had carried you somewhere new. About a second and a half after combat ends, the mod now describes where you actually ended up, staying silent if you are back somewhere already announced.
- Footstep silence now reliably means "you're not getting anywhere". Running against a wall used to keep firing steps: the engine alternates dead-stop and bounce frames at the point of contact, and the old per-frame speed test flipped with them (in a logged cave session, well over half the steps leaked through while grinding a wall). Suppression now judges net progress over the last half second with separate on/off thresholds, so grinding goes silent and stays silent — while a slide along a wall that genuinely covers ground keeps its steps.
- During combat, every footstep now plays. Circling an enemy at melee range covers almost no net ground and would read as "stuck", and the muting also swallowed the footsteps of further enemies closing in — so the blocked-cue stays an exploration feature and combat passes all steps through.
- The "no progress" direction probe finally fires. After about two seconds of pushing against an obstacle — or four seconds of circling inside a pocket — the mod names the directions that are still open. The announcer existed but its walking check demanded a footstep within 200 milliseconds, tighter than the natural step cadence, so it had never fired once.
- Tariga, the Sith archaeologist at the Valley of the Dark Lords entrance, has her lines read again. She uses a genuinely human character model but is voiced in alien Twi'leki, so the translated subtitle is the only understandable channel; she is now on the always-read list like the other alien-voiced exceptions.
- Adrenas, a Sith student in the Korriban academy, has his lines read too. Same situation as Tariga: a human character model, but the voice is alien Twi'leki, so the translated subtitle is the only understandable channel. He joins the always-read list.
- Yuthura Ban no longer has her lines read over her voice. She is a Twi'lek — her unique character model classifies as alien, which normally means "read the subtitle" — but she speaks fully voiced Basic, so the reading doubled her voice track. She joins the always-suppress list alongside Cassandra on Manaan.
- Room descriptions no longer ping-pong at region boundaries. Where a small region borders a large one — a crossing at the mouth of a wide cave chamber, say — half a step near the seam used to re-announce the two labels alternately, four times in sixteen seconds in one logged spot. A label now stays silent on the return leg: if you heard it within the last fifteen seconds and are still within four meters of where you heard it, it doesn't repeat. First-time announcements are never delayed or dropped, and walking properly back to a crossing still names it again.
- Camera-direction announcements stay quiet during cut-scenes. Scripted camera moves used to speak compass turns you never made; the turn announcer is now muted while the engine drives the camera and re-anchors silently to your real facing when control returns, so the first thing you hear afterwards is a turn you actually performed.

<h3>Installer:</h3>

- The installer now recognises when antivirus blocks the mod loader and tells you how to fix it. The mod loads itself through a file named `dinput8.dll`, and that name matches a pattern security software (Windows Defender, Kaspersky and others) often flags by mistake, so the drop can be blocked with a raw "access denied" that stopped the install with no explanation. The installer now retries briefly in case the block is a momentary scan-lock, clears any leftover read-only flag first, and — if it's still blocked — shows a dedicated message explaining it's almost certainly antivirus, confirming your game files are safe, and giving the exact steps to add a game-folder exclusion and re-run.

<h2>v0.5.8</h2>

<h3>Bug fixes:</h3>

- You can now reach every crystal, upgrade, and item in the workbench and equipment pickers. After the v0.5.7 crash fix moved crystals into a dedicated picker, some setups could still only land on the first item — pressing down briefly read the next one and snapped straight back, so the rest of the list stayed out of reach and Enter installed whichever item the selection had snapped back to. The picker now drives the game's own selection (the same path the on-screen scrollbar uses, so the highlight, the select sound, and multi-page scrolling are all the engine's) and keeps the mouse pointer off the list so the game's per-frame hover-tracking can't drag the selection back onto the row under the cursor. It only bit at screen resolutions where the parked pointer happened to fall on the list, which is why it surfaced for some players and never reproduced locally.

<h3>Navigation:</h3>

- Movement keys now cancel an auto-walk in progress. While the mod is walking you somewhere — Shift+- to a discovered object, or Enter to interact — pressing W, A, S or D (or your other movement keys) stops it at once and hands manual control back. Previously only a fresh key-press cancelled, so if you were already holding a key the auto-walk ran to the end; a long walk to a distant target could leave you unable to turn until it arrived. Movement the game itself drives — cut-scenes and scripted walks — is never cancelled, only walks the mod started.
- Auto-walking to an area transition now works. Doorways and area exits (such as the Tatooine Swoop Registration) are trigger zones that fire when you step into them, but the auto-walk was treating them as an object to "use" — which the engine can't path to — so it stalled and announced "way blocked" within a second or two. It now walks you to the spot the way it walks to a map marker, and crossing into the zone fires the transition. Fixed for both the Shift+- auto-walk and the Enter interact key.

<h2>v0.5.7</h2>

<h3>Bug fixes:</h3>

- Dialogue reply options no longer go missing in droid and computer hacking interfaces. When a conversation had more replies than fit in the on-screen list, the options scrolled off the visible page read as blank during navigation — so the lower-numbered choices spoke fine but the rest were silently skipped, most visibly the parts- and spike-cost repair actions in the droid-manipulation screens. The reply text is now read from the engine's own reply data, which always holds every active option, rather than from the on-screen rows, whose text only fills in for the lines currently drawn. It surfaced only for players whose screen resolution made the reply list scroll, which is why it never reproduced on a wider list.
- Attribute descriptions on the character-creation and level-up Attributes screen should no longer be shifted or mismatched. As you arrowed between Strength, Dexterity and the rest, the spoken description could belong to the neighbouring attribute instead of the one you were on — the description was driven by a mouse-hover hit-test whose result shifted with screen resolution, so it landed on the wrong row at scales other than the one it was tuned for. The description is now requested for the focused attribute directly, removing the resolution dependency the way the Skills, Feats and Force-power screens already do. (Fixed on inference — it never reproduced locally, so confirmation from an affected setup is welcome.)
- Upgrading a lightsaber or weapon at a workbench no longer crashes the game or reads out phantom duplicate entries. The compatible crystals and upgrades live in a picker you open by choosing a slot, but the same rows were also being folded into the screen's general keyboard navigation as duplicate buttons — including rows scrolled off the visible page, which the engine never finishes drawing. Arrowing onto one of those off-page phantom rows and pressing Enter could shut the game down, and the duplicates made the screen read inconsistently: extra controls, options that seemed to come and go, and odd readings when you re-entered. Crystals are now reached only through the picker — the same way the equipment screen already works — so the phantom rows are gone and the crash with them. (The picker itself navigates every crystal here, including lists that span more than one page; if you were previously unable to reach some crystals on a particular setup, a fresh log is welcome to confirm that symptom is resolved too.)

<h3>Swoop racing:</h3>

- Swoop racing is now playable by ear. Each accelerator pad sounds a panned tone that points to its lane — steer with A and D to centre it, and a short "on track" blip confirms the catch. Obstacles and the side walls warn with their own cues, and a separate cue tells you the moment you can shift up a gear (Space). Every swoop sound is listed in the audio glossary under Mod settings so you can learn them first.
- A steering-assist magnet nudges the bike onto the pad you're aiming at, so a near-miss still catches instead of sailing past.
- Race cues stay silent through the start countdown and begin the instant the race does.
- Swoop races no longer play a repeating acceleration tick that sped up as you went faster. It was meant to stand in for the on-screen speed bar, but the bike has no held throttle to track: it accelerates to each gear's top speed on its own, and you shift up with a single press of the accelerator. The tick modelled a control that doesn't exist, so it was just noise.

<h3>Installer:</h3>

- Downloads no longer fail with a "403" rate-limit error. The installer and in-game updater fetched every file through GitHub's REST API, which caps unauthenticated use at 60 requests per hour and counts them per network address — so anyone sharing an IP (mobile, campus, office, or VPN connections) could be blocked even on their first try, regardless of how small or rarely-downloaded the files are. Downloads now go through GitHub's direct release links, which aren't rate-limited, and only fall back to the API during an actual GitHub outage.
- The "Collect logs for beta test" bundle is now small enough to send over Discord directly — a typical bundle drops from ~64 MB to around 2 MB. Two changes do it: the crash dump is stripped down to just the parts we read during triage (the game's own code, all thread stacks, the crash-referenced data, and the module list), discarding the bulk — stock Windows and graphics-driver memory that's reconstructible and never inspected, which on a real dump was ~145 MB of ~151 MB; and the whole bundle is then packed as `.7z` (LZMA2) instead of `.zip`. Both steps fall back safely (full dump / plain `.zip`) if anything goes wrong, so the bundle is never empty.

<h2>v0.5.6</h2>

<h3>Bug fixes:</h3>

- Distant or awkwardly-placed containers and bodies now open reliably. Reaching for loot across rough or raised ground used to leave your character stopped short of the target without ever opening it; interaction now keeps walking until it's close enough to act.
- Starting a conversation now works on the first Enter, instead of sometimes needing the key pressed several times.
- Opening a container or using an object that takes a moment to play out no longer gets cut short with "way blocked". The walk-to watchdog that warns when something is genuinely unreachable could fire while you were standing right at the object mid-open, cancelling it before it finished. It now holds that warning unless it can actually confirm you're stranded out of reach.
- Examining an out-of-reach object now reads its result to the end instead of being cut off with "way blocked". Some objects sit off the walkable floor — the hovering Mandalorian swoop bikes on Tatooine, for instance — so your character can trigger them but never physically arrive. When such an object answered with a floating bark line rather than a full conversation, the walk-to watchdog mistook the unfinished approach for a dead end, cancelled it, and spoke "way blocked" over the line. It now treats the bark surfacing as proof the interaction fired and bows out quietly.
- Closing a mod menu with Escape no longer pops open the game's pause/Options menu. Escape now just closes the action menu (or the action-queue, examine, or help overlay) and drops you back into the world.
- Map-note hints no longer list notes for areas you haven't explored. The map-hint cycle now follows fog-of-war the way the game's own map does, so it surfaces only notes in regions you've actually uncovered.

<h3>Spatial cues:</h3>

- Wall, door, container, NPC, item and transition cues now fall off with distance — louder as you close in, quieter as you move away — so their volume tells you how near something is. They previously played at full volume anywhere within range, giving no sense of distance.
- Wall cues repeat less often when you maneuver back and forth in a tight space, where small movements used to set off a rapid flicker of pings from every side at once.
- Turn the camera to scan the space ahead. Walls and objects in your line of sight now announce as they pass through the front of your view while you turn, and a silent stretch marks an opening you can head for — so you can find your way by sweeping the view and walking into the quiet. This front-of-view cue previously tracked only your character's facing, which barely moves when you orbit the camera to look around, so turning in place to scan produced no sound.

<h3>Open-area navigation:</h3>

- Very large open spaces now announce as "large area" instead of "area". The label sets the expectation that wall and object cues are naturally sparse when you cross a space this big, so quiet stretches read as the size of the place rather than something gone wrong.
- Large open areas no longer flicker their region name. While crossing a big space such as the Kashyyyk Great Walkway, small gaps in the navigation graph between two named regions made the mod repeatedly announce a bare, nameless "area" and then re-announce the region on the far side, back and forth as you walked the seam. The mod now holds the last named region across those gaps, so you stay oriented instead of hearing the name drop out and return.

<h3>Combat:</h3>

- Queueing combat actions now announces the correct slot. Stacking actions on the bar — force powers, attacks, items — reads each at its true position ("Force Wave, position 1 … 2 … 3", then "queue full" on the fifth), because the cue now rides the engine's actual queue instead of a pre-press guess. Fast presses and key auto-repeat previously made the count skip, repeat, or fall silent, so queued actions seemed to vanish.
- A disabling status landing on someone in your party — stunned, poisoned, and the like — is now spoken urgently, cutting through queued combat chatter the way a defeat does; the damage that delivered it stays at normal priority. This matters most for a stun, which makes the engine clear that character's queued actions: hearing it immediately is your signal that the orders you stacked were just wiped.
- The action queue menu (Shift+H) now moves seamlessly to and from the other in-world menus instead of dropping the pause. Closing the queue with Escape while the action menu is still open underneath returns you there, still paused, and re-announces where you landed; and pressing a Shift+number from inside the queue switches straight to that action category. Previously Escape was the only way out of the queue and it always resumed the world, breaking a queueing session mid-flow.
- The in-world action menu now closes automatically when combat ends, so your next press interacts with the world again instead of landing on a leftover menu entry.
- Switching (Tab) to a party member who hasn't entered the fight no longer closes that menu, drops the pause, or wrongly announces the battle as over — it now says "Not in combat" while the fight continues. The mod judges combat from the whole party's state rather than the single character you control: the engine keeps one combat flag and re-points it to whoever you switch to, so a peaceful companion used to read as "combat ended" even mid-battle.

<h2>v0.5.5</h2>

<h3>Bug fixes:</h3>

- Komad Fortuna's lines are read on Kashyyyk. The Twi'lek hunter speaks an untranslated alien tongue, so his subtitles are your only channel — but on Kashyyyk the game gives him a different internal name than on Tatooine, where the mod already knew to read him, so his lines fell silent. He's now on the read-aloud list in both places.

<h2>v0.5.4</h2>

<h3>Bug fixes:</h3>

- The Force Powers screen at level-up reads its power tree again. A recent change to how the mod recognises the workbench screens accidentally matched the Force Powers picker too, so opening "Force Powers" while leveling up a Jedi fell silent — nothing read and you couldn't pick a power. The mod now identifies that screen unambiguously, so its powers, their availability, and descriptions speak as before.

<h2>v0.5.3</h2>

<h3>New features:</h3>

- Naming a saved game now works with the keyboard and screen reader. The "enter a name for your save" box reads as an input field with the name already in it, speaks each character as you type or delete, and re-reads the whole name on Up or Down; Enter confirms, Escape backs out. Previously the box was silent, so you were stuck with the default slot names.
- You can now reassign all of the mod's hotkeys — including to key combinations — from a new "Key bindings" entry in Mod Settings (Options → Mod Settings → Key bindings). Actions are grouped into categories (World and actions, Exploration and camera, Menus and input, Minigames, General); step into one, pick an action (read with its current key, e.g. "Level up: Shift+L"), press Enter, then press the key or combination you want. If it clashes with another mod action or one of the game's own keys, the mod names the conflict and waits for a different key; Escape cancels. "Restore defaults" resets every binding. Changes carry over across relaunches.
- The mod now warns you about key clashes with the game. The game doesn't know the mod's keys, so binding a game action onto a key a mod shortcut uses would silently fire both at once. Now, assigning such a key in the game's Key Mapping screen (Options → Game Settings → Key Mapping) speaks a warning naming the mod action it collides with; the mod's own Key bindings screen does the reverse, checking the key you pick against your live game keymap.
- The workbench now reads what an upgrade does, not just its flavour text. Lightsaber crystals and similar upgrades keep their gameplay effect (attack/damage bonuses, on-hit effects) in a part of the description the game skips for crystals, so Shift+Down used to read only a bare "Special:" heading. Now browsing a saber's crystals reads the full effect ("Special properties: Attack +1, Damage +1"), and a fitted crystal's effect can be heard straight from the slot without opening the picker.
- Item descriptions (Shift+Down/Up) are now split into sections you can step through instead of one long block. While holding Shift, Down moves to the next part and Up moves back, in this order, skipping any the item lacks: requirements and weapon class, combat values (damage, range, critical, attack and defence), special properties, then the flavour description. It clamps at the first and last part rather than wrapping. Works in inventory, stores and containers, equipped items and the equipment picker, workbench items, and quest items.
- German (and other localised) item descriptions now read with correct umlauts. Some items store their flavour text with every ä, ö, ü and ß mangled to the same garbled character, and that broken copy is the one the game hands out. The mod now reads the description from the master string table instead, where the text is intact, so "Energiestößen" or "natürlich" are spoken properly. Only the flavour description was affected.
- The workbench crystal picker now works like the equipment picker. The silent blank "remove upgrade" entry at the top of the list is hidden; to remove the fitted crystal, press Enter on it and hear "Upgrade removed". The fitted crystal (or current colour) is announced as "installed", and list positions no longer count the hidden entry. The colour-crystal slot, which has no remove option, still shows all its choices.

<h3>Bug fixes:</h3>

- Hacking and computer-terminal dialogue options are no longer read twice — two parts of the mod were both announcing each one. Each option is now read once with its position ("Hack the computer…, 1 of 2").
- Computer-terminal options are no longer wrongly announced as unavailable. The mod was checking a flag that tracks the highlighted row rather than whether an option can be picked; anything listed can be selected, since the game already omits choices you genuinely can't make.
- A mercenary on Manaan no longer has his lines read over his own voice. The voiced Republic survivor in the Hrakert Rift station was treated as a non-speaking alien because of his appearance; he's now on the voiced-character list, so his subtitles stay quiet.
- The Shift+L level-up shortcut no longer lets you level up endlessly. It opened the level-up screen even without enough experience, so you could apply level-ups past where the game should stop. It now respects the same condition as the character screen's "Level Up" button: with no level earned (or at the cap) it says "Not enough experience to level up yet" and does nothing.
- Repeated objects now get the same number from target-cycling (Q/E) and the in-world cycle (comma/period), and the number no longer shifts as you explore. When several things share a name, the mod numbers them ("Footlocker 2"). The two cycles used to number differently — Q/E by the order you looked at them, comma/period by position but counting only what you'd found, so finding another one renumbered the rest. Now both use one scheme: movable named characters keep an order-met number, and everything stationary is numbered north-to-south across the whole area. So the same footlocker is "Footlocker 3" from either key, every visit and after reloading — the trade-off being the first one you find might already be "3" if two unreached ones sit north of it.
- The 6 and 7 action keys are no longer crossed. Pressing 7 announced "Miscellaneous" but fired Explosives, and 6 did the reverse — most visible on the Manaan seabed, where 7 named the sonic emitter (your only usable item there) yet did nothing while 6 used it silently. The keys are now in line — 4 Force Powers, 5 Medical, 6 Miscellaneous, 7 Explosives — and the bare keys, Shift+number menus, and F1 list all agree with the game.
- Mod shortcuts using Shift, Ctrl or Alt no longer also fire the game's plain-key action while you're in the world. The game ignores modifiers (Shift+4 reads as 4, Shift+L as L), so a modified mod shortcut also triggered the bare-key action — Shift+4 opened the action submenu and queued a Force power, Shift+L opened level-up and the Quests journal. Now any key pressed with Shift, Ctrl or Alt in the world is reserved for the mod, so the game's shortcut fires only on the unmodified key. This also clears the way for reassignable hotkeys. (The same fix removes a phantom "Heal, slot 0" when opening a submenu with Shift+number.) Modified keys pressed while a game menu is open aren't covered yet.

<h2>v0.5.2</h2>

<h3>New features:</h3>

- A new "discovered objects" cycle that lets you find your way back to things you've already come across. The in-world cycle keys (comma and period) now resurface only the doors, containers, characters and landmarks you've actually encountered, instead of everything in the area at once — so you can return to a footlocker you spotted earlier or a character you've met without wading through the whole map. As you play, whenever the mod names something to you on its own — when you face it and it's read out, or when it calls out a nearby landmark — that object is quietly remembered for the area you're in. Comma and period step through that remembered set, grouped by kind and nearest-first as before, and Shift with them switches kind. The index is kept per area and stored inside your save game, so it survives quitting and reloading and travels with that save — reload and the things you'd found are still there to cycle. Nothing you haven't actually encountered appears, so there are no spoilers. If you'd rather browse everything in the area, including things you haven't found yet, turn on "Extended cycling" in Mod Settings and the same keys widen to list it all; turn it back off to return to just what you've discovered. (Loose items and ordinary enemies aren't tracked this way, since they move, respawn or vanish.)
- The "guide me there" key (Shift+Dash) now also walks you to map markers and other spots you can't interact with — and it routes around corners. Until now Shift+Dash only worked on things you can act on (doors, characters, containers): it walked you up to them along a proper path. Map markers you'd set yourself, and other targets with nothing to "use", were refused — it told you to use the beacon instead. Now Shift+Dash walks you to those too: focus a map marker (or any point you've cycled to) and press it, and the character sets off and finds its own way there, around walls and through doorways, the same way it does when you click a far-off door. As before, pressing Shift+Dash again while you're walking cancels and hands control back, and tapping a movement key stops it. Targets that are genuinely sealed off (no walkable route at all) still can't be reached — the character walks as far as the path allows.
- Every screen in the Options menu can now be read and adjusted with the keyboard. The settings sub-screens — Sound and Advanced Sound, Graphics and Advanced Graphics, Auto-Pause, Feedback, Gameplay, and Mouse — were untouched until now: arrowing through them you'd hit blank entries that read as "control 6" or "control 7", because each adjustable setting (anti-aliasing, texture quality, EAX, difficulty and so on) is a value flanked by two unlabelled arrow buttons that cluttered the list. Now each option reads its name and current value, you change a value with Left and Right the same way as every other setting, and the redundant arrow buttons are hidden so Up and Down step cleanly from one real setting to the next. Sliders, toggles and the per-option help text read as they do elsewhere.
- You can now read and change your key bindings in the Key Mapping screen (Options → Game Settings → Key Mapping). It works like the mod's other tabbed screens: you land on a short list — the three categories (Movement, Game, Minigames) followed by OK, Cancel and Default. Press Enter on a category to step through its actions, each read together with the key it's bound to ("Forward: W", "Run / Walk: R"); press Enter on an action and then press the key you want, and it's reassigned (if that key is already in use the game keeps waiting for another). Escape backs out of a category to the list; OK saves your changes, Cancel discards them, and Default restores the originals. Controls that can't be remapped are announced as such.
- The combat-behaviour picker on the character screen now works with the keyboard. The character sheet has a button (in German, "Kurzbefehle") that opens a small screen for choosing how a party member fights on their own — Standard Attack, Grenadier, or Jedi/Droid Support. Until now you could move the focus across the three options but couldn't actually choose one: pressing Enter just closed the screen and applied whatever was already set, so the behaviour could never be changed from the keyboard. Now Up and Down move the selection and read each option's name and position together with its full description, Enter confirms your choice and applies it, and Escape cancels without changing anything.
- Empty containers now tell you they're empty. When you focus or cycle to a lootable container (a footlocker, crate or corpse), the mod adds "empty" after its name — "Footlocker, empty" — when there's nothing left inside, so you no longer have to open it to find out it has already been looted or spawned bare. It's checked fresh each time, so the moment you take the last item the tag appears; a container that still holds something reads its name with no tag, as before, and things you can use but not loot (switches, computer panels) are never tagged.

<h3>Bug fixes:</h3>

- Talking to a distant character now walks you over and starts the conversation, instead of freezing you in place. When you pressed Enter to talk to someone more than about ten metres away, your character would stand frozen — unable to walk or open menus — for roughly four seconds, and the conversation never opened; you had to give up and try again. The mod was telling the game to start the dialogue but never letting it walk you into range first. It now lets the game's own "walk up, then talk" behaviour run — the same thing that happens when a sighted player clicks a far-off character — so you automatically walk into range and the conversation begins on its own, at any distance.
- When a character genuinely can't be reached on foot, the mod now cancels the walk and tells you, instead of leaving you stuck against the scenery. A few characters stand somewhere the walk can't quite reach — a judge behind a railing, for instance — so the automatic approach jams just short of talking range and keeps nudging you into the obstacle. The mod now notices when the walk has stalled and stops it, saying "Movement cancelled, way blocked" followed by the character's name, distance and compass direction so you know which way to move. Their conversation range is usually reachable from another angle, so once you walk close enough by your own route, talking works normally.
- The launch-time keyboard wake-up is now reliable, instead of sometimes still leaving the menus dead for several seconds. Version 0.5.1 made the mod wake the keyboard the moment the main menu appears, but on some machines that single wake-up didn't take — the game would quietly drop it a moment later (for example while it rebuilds its window), and the keyboard stayed dead until you Alt+Tabbed, just as before. The mod now keeps re-doing the wake-up until the menu actually responds to a key press, then stops, so input recovers on its own within a moment of the menu appearing. It only does this while the game is the window in front, so it never grabs the keyboard while you're working in another window.
- Loading a saved game no longer reads out a flood of unwanted messages. When you loaded a save, the mod would speak back the entire message log stored in it — every old combat result, item pickup, experience gain and journal entry — and then narrate each in-game screen the game silently rebuilds while loading (build text, panel titles like "Buy" or "Options", and stray "control 8"-style placeholders), before you finally heard which area you'd arrived in. Loading a save from inside a game also read out your character's name on top. Now loading is quiet: the replayed message history, the screen-rebuild chatter and the spurious character-name announcement are all suppressed, and you simply hear the name of the area you loaded into. This works whether you load from the main menu, load another save while already playing, or use the quick-load key.
- The action menu no longer always pauses the game — it now follows your Auto-Pause settings, like the game's own does. The mod's action menu (Shift+Enter, or Shift plus a number) froze the world every time it opened, regardless of your settings. In the unmodded game, opening the action menu only pauses when you've turned on the "Action Menu" option under Options → Auto-Pause, which is off by default. The mod now matches that: with the option off, the menu opens without pausing and the world keeps running while you browse it (and Escape says "Action menu closed" so you still know it's dismissed); with the option on, it pauses on open and resumes when you close it, exactly as before. Turn the "Action Menu" Auto-Pause option on if you prefer the world to freeze while you pick an action.
- You can now set your Pazaak bet from the keyboard, with a tap for fine changes and a hold to run it up or down quickly. Building your side deck and reading the wager popup already worked, but the bet amount itself wouldn't move from the keyboard, so you were stuck at the opening bet. Now, with the wager popup open, Left and Right change the bet: a single tap moves it by one credit, and holding either arrow auto-repeats with increasing speed, so you can race the bet up to the table maximum (or down to the minimum of one) in a second or two — you hear a rapid click as it moves and the final amount spoken once when you let go. Up and Down step cleanly between the bet line and the confirm and quit buttons, without stopping on the old increase/decrease arrows.

<h2>v0.5.1</h2>

<h3>New features:</h3>

- A new key list you can open anywhere with F1. Press F1 — in the world, in any menu, in a conversation, on the map — and the mod reads a grouped list of every important key, both the mod's own keys and the game's. Up and down arrows move through it, Home and End jump to the start or end, Enter repeats the current line, and Escape or F1 again closes it. It's meant as a reference for new players who don't yet know the controls: the list is organised into sections (navigation, movement, targeting, combat, screens, map and so on) so related keys are read together, and shared keys like the arrows are mentioned once rather than repeated in every section.
- A second key, Ctrl+F1, reads just the keys that matter on the screen you're on right now. Where F1 lists everything, Ctrl+F1 speaks a short summary tailored to your situation — the in-world keys when you're walking around, the menu keys when a menu is open, the map keys on the map, the action-menu keys when it's up, and so on. It's the same idea as the cue the Pazaak board already speaks when it opens, now available on demand across the game.

<h3>Bug fixes:</h3>

- If Steam Big Picture Mode is sitting in front of the game, the mod now tells you why your keys aren't working instead of leaving you stuck. When the game runs in a window and Steam Big Picture Mode is in the foreground, your key presses go to Big Picture rather than the game, so menus seem completely dead — you press keys and nothing moves, even though the mod still reads the screen. The mod now detects this and, the moment you press a key while Big Picture has the screen, says "The game can't receive your key presses because Steam Big Picture Mode is in front." It speaks the warning at most once every twenty seconds so it doesn't nag, and it only describes the problem — what to do about Big Picture is left to you.
- The keyboard now works in the menus right after launch, instead of staying dead until you Alt+Tab. On some machines the game reaches the main menu with its keyboard quietly unacquired — the mouse works, but arrow keys and the mod's menu keys do nothing, sometimes for twenty seconds or more, until you Alt+Tab out and back in to wake it. The mod now performs that wake-up for you the moment the menu appears (when the game has the foreground), so the menus respond to the keyboard straight away.
- Arrow keys and menu keys no longer drive the game while you're working in another window. The game holds the keyboard at a level that keeps reading it even when it isn't the window in front — so, screen-reader users especially, pressing arrows in another window (your screen reader's own window, a spreadsheet, anything) would navigate the game's menus at the same time. The mod now releases the keyboard whenever the game loses the foreground and takes it back when the game returns, so the game only responds to the keyboard while it's actually the window you're in. As part of this the mod also stopped pulling the game window back to the front by itself at startup, which could yank you out of whatever window you had switched to.

<h2>v0.5</h2>

<h3>New features:</h3>

- You can now repeat the current line of dialogue by pressing R during a conversation. If you missed what a character just said, R reads their last line again. It also works as a way to hear voiced lines the mod normally skips: when reading of voiced subtitles is turned off, the mod stays quiet for fully voiced human and droid speakers so it doesn't talk over a voice you can already hear — but if you didn't catch one of those lines, pressing R reads it on demand. R only does this while a conversation is on screen; outside dialogue it keeps its normal in-game meaning.

<h3>Bug fixes:</h3>

- Large inventories now list every item, and the credits readout is back. The inventory screen gives you a list to arrow through — all your items, plus a "Credits: N" line at the top showing your money. That list had a hidden limit of 64 entries: once you were carrying more than about 64 items it filled up before the credits line could be added, so the credits reading silently vanished — and any items past the 64th were unreachable as well. The limit is now raised far beyond any realistic inventory, so every item is listed no matter how much you carry and the credits line is always there again. (The shop screen was never affected, because merchants stock fewer items.)
- Spoken bark lines — the short one-liners characters call out as you walk past — now follow the same voiced-subtitle rules as full conversations. Until now every bark was read aloud no matter who said it, so a fully voiced character's bark was spoken on top of the voice you could already hear, even though the mod correctly stays quiet for that same character in a normal conversation. Barks now identify their speaker the way dialogue does: a fully voiced human or droid bark is skipped (when reading of voiced subtitles is turned off), while system and loudspeaker announcements — which have no speaker — are always read.
- Some characters now have their subtitles read or skipped correctly. The mod decides whether to read a character's subtitle from how they appear, and a handful of characters were judged wrongly: some who speak in a language you can't understand were having their subtitles skipped, leaving you no way to follow them, while some fully voiced characters were being read aloud over their own speech. These are now sorted correctly, so each character's subtitle is read or skipped to match the way they actually speak.
- Persuade and other skill checks now announce their outcome even when the spoken line is skipped. When you talk your way through a check — Persuade, Computer Use, Repair and the like — the game marks the response as a success or failure (for example "[Success]" or "[Failure]" at the start of the line). If that line belonged to a fully voiced speaker whose subtitle was being skipped, the success/failure tag was skipped with it and you never learned the result. The mod now reads just that outcome tag, in your own language, while still staying quiet for the rest of the voiced line.
- The keyboard should no longer stop responding after the game loses and regains focus. If another program repeatedly steals focus while the game runs in a window — an overlay, a screen recorder, or a misbehaving background app — the game can recreate its window and quietly drop all keyboard input, leaving menus and even the quit confirmation unresponsive while the mod still reads the screen. The mod now re-enables keyboard input each time the game window returns to the foreground, so input should recover on its own. (Running the game in full-screen also avoids the problem.)
- The camera should no longer start spinning on its own with no way to stop it. On some loads the mouse cursor ended up parked against the very edge of the screen, where the game treats it as a continuous "turn the camera" command — so the view would spin endlessly even though you weren't touching anything, and only restarting the game (or nudging a real mouse or touchpad) would stop it. The mod now notices when the cursor is stuck at the screen edge while the camera is turning and pulls it back to the centre, stopping the runaway. This one was hard to reproduce reliably, so it's marked as hopefully-fixed — if it ever happens again, the mod now records exactly what occurred so it can be pinned down.
- English combat narration now actually works, instead of reading the full combat log out loud. Version 0.4.4 added the mod's short combat narration — who hit whom for how much, plus force-power and grenade effects — for English alongside the other languages, but in English it never actually engaged: a mismatch in how the mod recognised the English "Hit/Miss with <weapon>" wording meant every English combat line slipped through and was read out as the game's full, unabridged log instead. English now gets the same condensed combat narration as German, French, Italian and Spanish.

<h3>Navigation:</h3>

- The way the mod describes the space around you has been reworked. Until now it called out individual features as you moved — announcing "open space" or a "place" — which often broke a single room up into several separate callouts. That is replaced by a single area announcement: when you enter a distinct space the mod now names it as an area, tells you its shape when it is clearly elongated (for example a long north-south stretch), and lists the exits leading out of it. A number of bugs in how the mod works out an area's shape and where its exits are have been fixed, and the foundation the whole system is built on has been made more reliable. This is a clear step up from before, but area descriptions are still being refined and will keep getting more accurate in future versions.

<h2>v0.4.4</h2>

<h3>New features:</h3>

- You can now open the action menu directly while another menu is on screen. Pressing an action-menu key — Shift+Enter, or Shift+1 through Shift+7 — while you are in the inventory, map, journal, options or any other in-game menu now closes that menu and opens the action menu, the same way the game's own menu keys switch you straight from one screen to another. Pop-up boxes (such as a save or quit confirmation) still block it, exactly as they block the game's menu keys.

<h3>Bug fixes:</h3>

- Combat narration is now shortened in English, French, Italian and Spanish, not just German. The mod condenses the game's verbose combat log into short spoken lines — who hit whom for how much, plus force-power and grenade effects, saving throws, damage absorbed and kills. A recent rework of that shortening had been wired up for German only, so players on the other four supported languages heard the full, unabridged combat log read out line by line. All five supported languages now get the same condensed narration. (As a safeguard, any combat line the mod doesn't recognise is still read out in full, so nothing is ever lost.)
- The action menu now steps aside for pop-up boxes and other screens instead of fighting them. When a message box (such as the quit confirmation) or another menu came up while the action menu was open, both reacted to the same arrow and Enter presses — so navigating the box also moved the hidden action menu, and Escape closed the menu rather than the box. The action menu now pauses itself while a pop-up or menu is in front of it, leaving those keys to the box, and when you close the box it returns to exactly the category and entry you were on — the same way the game's own menus come back after a pop-up.

- The action menu is now truly unified no matter which key opens it. Opening it at a personal category (Shift+4 through Shift+7 — your own Force powers, medical items, miscellaneous items, explosives) while an enemy is targeted now lets you arrow left and right into the target's categories too — its attacks, the Force powers you can aim at it, and throwable items like grenades. Before, opening at a personal column trapped you among only the personal categories: arrowing toward the target options hit a dead end, and you had to close the menu and reopen it with a different key to reach the enemy's attacks or throw a grenade at it. With no enemy targeted the menu still opens to your personal categories alone, so self-buffs work as before.
- Queuing an action with a number key while the action menu is open is now read out. With the menu open and paused, pressing a number key to stack up an action (for example tapping 1 a few times to line up basic attacks before backing out) queued the action but said nothing, so you couldn't tell what you'd added or how many were in the queue. Each press now speaks the action and its place in the queue ("Power Attack, slot 2"), the same as queuing from normal gameplay, and tells you when the queue is full.
- Certain characters' subtitles are now always read aloud, even with voiced-subtitle reading turned off. To avoid talking over speech you can already hear, the mod normally skips reading the subtitles of fully voiced human and droid speakers. But a few characters carry meaning only in their on-screen text — the voice alone doesn't convey it — so skipping their subtitle would leave you missing what they say. The mod can now exempt specific characters from subtitle hiding, and those characters' lines are always spoken.

<h2>v0.4.3</h2>

<h3>New features:</h3>

- The galaxy map (the star-map travel screen you use to fly between planets) is now fully usable with the keyboard. Press Up and Down to move through the planets you can travel to — each one's name is read out as you land on it, and worlds you haven't unlocked yet are skipped over, so you only hear the ones you can actually reach. Shift+Down reads the selected planet's description. Press Enter to travel to the highlighted planet, or Escape to back out. Before, the whole screen was a grid of unlabeled buttons that all read as "control" plus a number, with no way to tell the planets apart, know which were reachable, or hear where you were about to fly.

<h2>v0.4.2</h2>

<h3>Bug fixes:</h3>

- Leveling up no longer loses the Force power you chose. On the level-up screen, if you picked a power and then opened another category (such as Skills or Feats) before pressing Accept, the level still completed but the power was silently discarded — you spent the level and learned nothing. The screen has a required order, and opening a category out of that order is what threw the pick away. The mod now keeps to that order: only the category the game has unlocked next can be opened, and pressing a still-locked one now tells you which step to finish first. Powers you pick now always stick.
- The keyboard no longer goes dead after loading a save. If you pressed any keys while a save was still loading, the game could come up completely unresponsive — no movement, no menu navigation, not even Escape to open the menu — and the only way out was to alt-tab out of the game and back in. The cause was the keyboard input being left switched off after the load if a key was pressed while the game was rebuilding its window mid-load. Loading now re-arms keyboard and mouse input the moment the area finishes loading, so input always works whether or not you touched the keys during the load.

<h3>Level-up and character creation:</h3>

- The level-up and character-creation screens now tell you which step to do next and which steps you can't reach yet. The step the game has unlocked is read out plainly, while categories that aren't your turn yet — or that you have no points to spend on — are announced as "unavailable". Before, every category sounded the same, so reaching one that did nothing when you pressed it made the screen feel broken.

<h2>v0.4.1</h2>

<h3>Bug fixes:</h3>

- The Force Points readout (the H key self-status) now reports your real current Force points. It previously always spoke a fixed number regardless of how much Force you actually had or had spent — it was reading a static base value instead of the live pool. It now reads the same current-Force value the character sheet shows, so it updates as you cast powers and rest. Maximum Force points and current/maximum health were already correct and are unchanged.
- An enemy's health status is finally announced correctly. When you cycle to or target a creature, the mod speaks the same wound state a sighted player reads from its health bar — lightly wounded, wounded, badly wounded, dying, or dead — and updates as the fight goes on. Previously only "dying" ever came through; every other wound state was silently dropped, so a half-dead enemy sounded unharmed. A full-health enemy still says nothing about its condition (just as a full bar tells a sighted player nothing new).

<h3>Updater:</h3>

- The in-game updater now downloads new versions reliably even when GitHub is having problems. The "Update available" notice and the F5-to-install flow already worked, but the actual download went through GitHub's public browser download link, which during a partial GitHub outage returns an error and failed the whole update — this is exactly what stopped the first 0.4 download. It now fetches the installer through GitHub's API instead, which stays up during those outages and is the same path GitHub's own tools use, so an update that is offered will actually download.
- The spoken feedback while updating is clearer. Pressing F5 to start an update now says "Starting download." as it begins, and if the download fails it now says it failed and that you can press F5 to try again — previously it only said the download had failed, with no hint that another press would retry.

<h3>Action menu:</h3>

- Choosing an action in the action menu now queues it and keeps the menu open, instead of performing it and resuming the game right away. This lets you line up several actions for a character in one pass — throw a grenade, then cast a Force power, then attack — without the menu closing and the world unpausing after each choice. Each Enter adds the action and tells you its place in the queue ("Force Valor, slot 1"); the game stays paused so you can keep going. Press Escape to close the menu and resume, and the queued actions run in the order you chose them. The trade-off is that a single quick action out of combat — a heal, say — now needs an Escape afterward to resume, where it used to resume on its own; queuing several actions no longer means re-pausing between each is the bigger win.
- Queued actions now actually stack instead of overwriting each other. Each action you chose used to wipe the character's existing action queue before adding itself, so lining up three actions left only the last one to run. Selecting an action now appends to the queue the way the game's own Shift-click queuing does, so a grenade, a Force power and an attack all run in turn.

<h2>v0.4</h2>

<h3>Action menu:</h3>

- The in-world action menus are now one menu you navigate the same way every time. Combat actions used to be spread across three separate menus with different shapes — the radial (Shift+Enter), the target-action menus (Shift+1, Shift+2, Shift+3) and the personal action bar (Shift+4 to Shift+7). They are now a single menu organised into named categories: for whatever you have targeted, "Attacks", "Force Powers" and "Items"; and your own "Self Powers", "Medical", "Miscellaneous" and "Explosives". Left and Right move between categories, Up and Down move between the entries inside the current category, Home and End jump to the first and last entry, and Ctrl+Home and Ctrl+End jump to the first and last category. Shift with any arrow reads the full description of the entry you are on without choosing it. Enter performs the highlighted action and closes the menu; Escape closes it without doing anything. Each category announces itself by name as you reach it, so you always know where you are. The reason for the change is that blind players do not need the visual target-versus-self split the game draws on screen — one menu with spoken category names is a single thing to learn instead of three.
- You can still open the menu straight at the part you want. Shift+Enter opens it on the target's actions, the way the radial did; Shift+1, Shift+2 and Shift+3 open it directly on a target category; and Shift+4 to Shift+7 open it directly on one of your own categories — so the keys you already know take you to the same place, now inside one consistent menu, and from there Left and Right reach everything else. If a category you open is empty — your own force powers on a character who has none, or explosives when you carry no grenades — it now says so by name ("Self Powers: empty") instead of quietly opening a different category. Pressing a number on its own, without Shift, still instantly performs that action exactly as before; only the Shift versions open the menu.

<h3>UI:</h3>

- The workbench upgrade screen now tells you what is in each slot and what it does. Arrowing through a weapon or armour's upgrade slots used to read only the slot type ("Energy Cell", "Armor Reinforcement"), with no way to tell whether a slot was empty or already held an upgrade. Each slot now also reads its state: "empty" when empty, or "occupied with" followed by the installed upgrade's name when occupied. Pressing Shift+Up or Shift+Down on a slot reads the full description of the upgrade installed in it — the same bonuses and effects you would see hovering it — so you can check what each fitted mod is doing, the same way item tooltips read elsewhere; on an empty slot it tells you the slot is empty.
- Force points now read for Force-users: the H status readout speaks your Force points, and the character sheet reads them next to hit points for a Jedi while dropping the line entirely for non-Force characters (Carth, droids) instead of showing a meaningless number, updating live as you Tab between party members.
- Stacked and limited-use items now read their quantity everywhere they appear. The inventory, containers and the merchant screen already spoke how many of a stacking item you had ("3 in stack"); that count now also reads in the in-world action menu, so a stack of medpacs or grenades reads as "Medpac, 3 in stack" while you arrow through your Items. Items that hold a fixed number of charges rather than stacking — and so never showed a count at all — now read their remaining charges ("4 charges") in all of those same places. Charged items cannot stack, so the two counts never collide.
- The party selection screen now keeps you up to date as you build your group. Each companion's portrait reads whether they are in the team or available on the bench, but until now that status was fixed the moment the screen opened — selecting or removing someone did not change what it said, so it was easy to lose track of who was actually in and end up reopening the screen to be sure. The status now updates the instant you add or remove a companion and reads the new state aloud ("Mission Vao, in party" / "Carth, available"). And when your party is already full and you press Enter to add a third companion — which the game silently refuses — it now says "Party full" instead of doing nothing, so you know to remove someone first.

<h3>Audio:</h3>

- You can now set the volume of the spoken announcements. The mod's urgent spoken cues — compass turns, the map and region cursor while panning, walking cues and similar — are voiced through a separate speech channel that bypasses your screen reader, and until now they always played at full volume with no way to turn them down. A new "Spoken announcement volume" slider under Mod settings now sets their volume from 0 to 100 percent (default 100); arrow left and right to adjust in steps of ten, and each step speaks a short sample at the new level so you can hear it. The setting is remembered across launches. This is separate from the existing hint-sound volume slider, which controls the non-spoken cue sounds, and from your screen reader's own volume, which still governs ordinary menu and reading speech.

<h3>Combat:</h3>

- Combat announcements have been rewritten to speak only what you need to react to in the moment. The game's own message log still records every hit in full — to-hit rolls, defence breakdowns, damage components — and you can pause and read it there at leisure exactly as before; nothing is removed from it. What is *spoken aloud* during the fight is now a short result instead of the full breakdown: who did what, the damage and its type, and any status effect. The long roll-by-roll statistics are no longer read over the action, so a busy round no longer buries you in numbers you cannot act on in six seconds.
- Your own attacks are now spoken, not only the hits you take. A party member's special move — a feat or a force-delivered attack — is read whether it lands or misses, and is named; ordinary auto-attacks are read when they connect (a plain miss stays silent, as it costs you nothing). Hits landing on your party are read as before, and now correctly include your own character, not just companions. Attacks between two enemies stay quiet.
- Grenades and force powers now read as a single short result per target, naming the power and the saving throw involved — instead of reading the raw saving-throw roll, which spelled out the dice, modifiers and difficulty class and was hard to follow mid-fight. For each target you hear whether they resisted and which save it was: for example "Kath hound resists Frag Grenade, Reflex" when they make the save, or "Kath hound: 20 Physical from Frag Grenade, Reflex failed" when they fail it. The save type is always named — Reflex, Fortitude or Will — so you can tell what was rolled against what, where before the saving-throw line read like unexplained numbers. Powers cast normally name themselves; a few the game logs without a "uses…" line (such as a stun queued while the game is paused) can only report the save result without the power's name.
- The critical-hit confirmation roll is no longer read aloud. When you score a possible critical the game makes a second "threat" roll to confirm it (the threat-roll line); its math used to be read out and was confusing on its own. The outcome it decides is already in the attack report — a confirmed critical is spoken as "critical" — so the separate roll is now left to the message log.
- Repeated damage-absorption messages are gathered into one. When a shield or damage resistance is soaking hits the game prints a line for every blocked hit, so a sustained barrage produced a stream of identical "absorbs 5 points" messages. These are now summed into a single spoken total once the barrage settles, so you still hear that your protection is working and roughly how much it stopped, without the repetition.
- Defeating an enemy is now announced promptly on the urgent speech channel, so a kill and the experience gained cut through rather than waiting behind other combat speech.
- The mod's own in-world menus now pause the game while they are open, the way the built-in in-game screens do. Opening the action menu (Shift+Enter or Shift+1–7), the action-queue review (Shift+H) or the examine view (Ö) freezes the world, and closing it resumes. Previously these menus left the game running underneath you, so combat and movement carried on while you were reading or choosing; now you get the same breathing room the built-in menus give. The pause is silent — the menu's own opening speech is the cue — and switching straight from one of these menus to another stays paused.

<h3>Dialogue:</h3>

- Droid speech is now suppressed under its own voice, the same as human speech. The subtitle filter (on by default) skips reading a line aloud when you can already hear it spoken — until now that covered human speakers but not droids, so the screen reader read droid subtitles on top of their audio. Droids are now treated the same way: HK-47's spoken Basic lines stay quiet so you hear his actual voice instead of TTS over it, and T3-M4 and other binary-only droids no longer have their beep-and-whistle subtitles ("Beep. Whoop. Weep.") read aloud. Nothing is lost by skipping those — a binary droid's meaning comes through your own reply choices, which are still read, not through the beeps themselves. Because the filter now covers every voiced speaker, its toggle under Mod settings is relabelled "Read voiced-speaker subtitles"; turn it on to have all subtitles read regardless. Genuinely alien speech you cannot otherwise follow — Zaalbar's Shyriiwook, background Twi'lek chatter — is still read as before.

<h3>Screen-reader compatibility:</h3>

- Experimental: the game should no longer crash on startup for people who use a screen reader other than NVDA. A few testers reported the game closing instantly at launch — once with ZDSR installed, and one report with JAWS. The cause was the speech layer trying every screen reader it can find, in order, until one answers; when it reached a reader whose support files on that PC didn't match what the speech library expected, the whole game crashed instead of moving on. NVDA users never saw it because NVDA is tried first and answers before the broken one is reached. The mod now skips any screen reader that fails this way and moves on to the next, finally falling back to the built-in Windows voice (SAPI) so you always get speech rather than a crash. This is marked experimental because it cannot be reproduced or verified on the developer's machine — if you hit the startup crash, please install this build and send a fresh log so we can confirm it is fixed.
- The mod now defaults to English speech labels when it can't determine the game's installed language, instead of defaulting to German. This only affects installs in a language the mod doesn't yet have labels for (for example Polish), or where language detection fails; a correctly installed English, German, French, Italian or Spanish copy is detected and read in that language as before.

<h3>Bug fixes:</h3>

- The main menu now takes keyboard input on a fresh launch without needing to alt-tab. On many machines the game reached the main menu with its keyboard asleep — you could hear the menu, but the arrow keys and Enter did nothing until you alt-tabbed out and back once. There were two causes. The game only wakes its keyboard when its window is activated, and on a cold launch that activation could be missed, leaving input dead; and the Windows Xbox Game Bar popup (and similar overlays) can grab the window focus for a few seconds right as the menu appears, which stops the game from claiming the keyboard at all. The mod now wakes the keyboard itself the moment the main menu first appears, and for about fifteen seconds afterwards it pulls the game back to the front if an overlay like Game Bar steals focus — so input works straight away. After that short window it never touches focus again, so you can still alt-tab away whenever you like, and Game Bar still works normally if you use it.
- The combat action-queue review has moved to a new key, and so has target Examine. The queue review used to open with Shift+K — but the game reads K as its own Skills / Feats / Force Powers screen and ignores the Shift, so every Shift+K also popped that screen open; this was most obvious when the queue was empty and nothing else happened. The queue review now opens with Shift+H, a key the game leaves alone, so it no longer triggers anything else. To free up Shift+H, the target Examine readout that used to live there has moved to the Ö key — pressed on its own, no Shift, the key your right little finger already rests on — and still toggles open and closed the same way.
- Fixed the action-queue review (now Shift+H) wrongly saying "Action queue is empty" while you actually had actions queued. It was reading the wrong characters' queues: it walked your companions but skipped your own main character entirely (the player is not held in the party roster the same way companions are), so any actions you had lined up on the character you control were invisible to it. It now reads your controlled character's queue first and your companions' after, so your queued actions appear and can be reviewed or cleared as intended.
- Pressing Escape to close one of the mod's own in-world menus — the action menu (Shift+Enter or Shift+1–7), the action-queue review (Shift+H) or the examine view (Ö) — no longer also pops the game's pause/Options menu open on top of it. Escape now just closes the menu. These menus are drawn by the mod and have no game window of their own, so the press slipped through to the game's built-in "Escape opens the menu" handler underneath; that press is now caught while one of the menus is open, so it only closes the menu — the same way Escape already backs out of the game's own screens without side effects.
- Fixed getting stuck unable to move after trying to talk to someone from a distance — the "Janice bug" several testers ran into. When you press Enter to talk to (or use) something too far to reach, the game walks your character over first. The mod briefly hands movement to the game while that walk happens, then takes it back; but it was guessing when the walk had finished with a fixed three-second timer, and every extra Enter press you made while waiting pushed that timer further out. So if a conversation was slow to start and you pressed Enter again — as you naturally would when nothing seems to happen — your character could end up frozen in place, unable to walk, until you stopped pressing entirely. The mod now watches the game's own action queue instead of guessing: control returns the instant the walk actually finishes, and repeated presses no longer extend the wait. This also fixes the same freeze after any long auto-walk — pressing Enter to reach a far object, or using extended cycling to travel a long way: the walk is no longer cut short by the old timer, and no longer leaves you stuck if it runs longer than a few seconds.

<h2>v0.3.1</h2>

<h3>Navigation:</h3>

- Your party members no longer make the proximity sound cue as you walk around them. The previous release stopped companions from setting off the focus "person nearby" cue, but a second, continuous cue — the proximity beacon that pulses for nearby creatures — was still firing for them, so in an otherwise empty area you would hear a steady person signal from Carth or Mission trailing behind you. Companions are now left out of that beacon as well. Other creatures, including enemies, still pulse so you can hear them coming, and targeting a companion with Q / E still reads their name and status.
- Pressing Tab to hear who you are leading now reads your character's real name. While you were controlling your own character it read a leftover placeholder ("test") instead of the name you chose at character creation; your companions read correctly. It now reads your chosen name.
- Map hints and cycled objects that share a name are now numbered, so you can tell them apart and refer back to a specific one. Some map markers repeat the same label along a path — four "North Path" hints on Dantooine, for example — and cycling with comma / period (or Q / E in the world) read the same name over and over with no way to distinguish them. Same-named entries now get a number: "North Path 1", "North Path 2", and so on. Fixed things — map hints, doors, footlockers, area transitions — are numbered by location from north to south, so the northernmost is 1. Because the number comes from the spot itself and not from how close you are, the same marker always carries the same number: on every visit, in every save, and for every player, no matter which direction you arrive from. Creatures can move, so they instead keep a single fixed number across the comma / period cycle, Q / E and the combat log.

<h3>Minigames:</h3>

- The Ebon Hawk turret-defense aiming is much better, and it can now be played by ear. The previous release's cue pointed the wrong way — it followed a guess at where your shots were going that turned out to be far off, so following it rarely lined you up and hitting a fighter was mostly luck. The aiming has been rebuilt on how the turret actually works: the cue now follows your gun's true line of fire, and an aim-assist gently pulls your aim onto the fighter you have locked (with Q / E) as you swing near it — the way aim-assist works with a console controller, stronger the closer you already are. Manual aiming is now genuinely playable rather than down to chance. The cue still pulses when your aim is off, pointing the way to swing, and goes solid when you are on target so you know when to fire; Q / E cycles which fighter you are locked onto.
- The "Autoaiming" easy-mode option, under Mod settings, now works. It was wired up but non-functional before; switch it on and the turret tracks your locked fighter and fires on it by itself, clearing a wave in seconds — for when you would rather not aim at all.

<h2>v0.3.0</h2>

<h3>Dialogue:</h3>

- More human characters now stay quiet under their own voice. With the human-speaker subtitle filter on (the default), the mod skips reading a subtitle out loud when the character is already speaking it in a voice you can understand, so the screen reader no longer talks over the recorded line. Two gaps in that filter are now closed. First, several voiced human characters — Vrook Lamar on Dantooine among them — were wrongly tagged as alien internally, so their subtitles were still being read over their voice; they're now recognised as human and stay silent. Second, conversations you only overhear — where two characters talk to each other and you aren't part of the exchange, like the Taris cantina scene — were always read aloud, because the mod could only identify the speaker when you were the one being spoken to; it now identifies the speaker on every line, so overheard human dialogue is suppressed the same way. As before, this only affects voiced speech you can already understand: most alien and droid speech, and anything unvoiced, is still read, and you can turn the whole filter off under Mod settings to have every subtitle read regardless.
- Non-human party members who speak Basic now have their subtitles suppressed under their own voice too. Mission (a Twi'lek) and Juhani (a Cathar) speak Basic with full voice acting, but the subtitle filter only recognised human characters, so the screen reader read their lines aloud over their recorded voices. They're now treated like human speakers and stay quiet. Genuinely alien speech you can't follow otherwise — Zaalbar's Shyriiwook, background Twi'lek chatter — is still read as before.
- Choosing a dialogue reply with a number key no longer also fires a combat action. Pressing 1–7 to pick a reply was additionally triggering the matching action-bar or target hotkey — attacking, using an item, or speaking a phantom "used" cue — because the mod kept refreshing the combat action bar on those keys even mid-conversation. Number keys now only select the reply while a dialogue is open.
- Dialogue no longer announces how many replies are available when a conversation node opens. The screen reader read out a count like "3 replies available" on top of reading each option as you arrow through them; since the options are numbered anyway, the count was redundant and is removed.

<h3>Navigation:</h3>

- Your own party members no longer set off the "person nearby" cue and name announcement as they wander around you. While exploring, the passive narration kept locking onto your companions — Carth, Mission, whoever is travelling with you — as they walked past or ahead of you, replaying the person cue and reading their name over and over. They're now skipped while you move around. You can still target a companion on purpose with Q / E, which reads their name and status as before (just without the person cue), so checking on a companion or talking to one still works.
- The navigation beacon can now be heard when the next waypoint is far away. The beacon's directional pulse is tuned for the short range the game's other cues use, so once a waypoint was more than about twenty metres off its pulse dropped below hearing and you lost the direction to follow. Distant waypoints now stay audible, with the pulse still pointing the right way and growing louder as you close in.

<h3>Combat:</h3>

- Combat now tells you when one of your own party members is hit. You hear a short report — who was struck, how much damage, and who hit them (with a "critical" note when it applies). These hits were being dropped silently until now: internally the mod could never tell which creatures were actually in your party, so it never recognised a companion as the one taking the blow. That detection is fixed, so hits landing on your companions are read out.

<h3>Minigames:</h3>

- The Ebon Hawk turret-defense sequences now play a sound cue to help you aim at incoming fighters. The cue tracks your locked target — pulsing when your aim is off and pointing the way to swing, going solid when you're on target so you know when to fire. This is an early, rough pass: landing a hit is still largely down to luck and the cue needs sharpening in a future update. An auto-aim easy-mode option is wired into Mod settings but is currently broken; it'll be fixed alongside the aiming improvements.

<h3>UI:</h3>

- The small "OK" notification popups — a new journal entry, credits or experience gained, light- or dark-side points, items received or lost — now read their message. The screen reader reads the notification aloud as the popup appears (for example "New Journal Entry" when a quest updates), and you can arrow up to the text to hear it again before pressing OK, the same way the confirmation message boxes work. Until now only the OK button was readable and the message itself was silent. When a popup carries several notifications at once each line is read, and only the lines that actually apply are read.
- The "Close" button no longer shows up when you arrow through a sub-screen. Every menu's close button does exactly what Escape already does, so landing on it was just an extra dead stop on the way down the list. It's now skipped in Character, Abilities, Inventory, Equipment, the Journal and its Quest Items screen, the shop, every Options screen, and the rest — in all languages. Escape still closes each screen as before, and confirmation popups keep their Cancel / No button (only the standalone Close button is removed).
- The journal's quest list stays readable after you sort it or swap between active and completed quests. Previously a sort could leave entries reading as "control 1", "control 2", … and Enter would stop reading the quest text; both work again now, every time you re-sort or swap.
- The Quest Items screen — opened from inside the journal — is now accessible. Its title is read when you open it, and Enter (or Shift+Up / Shift+Down) on a quest item reads that item's description, the same way item tooltips read elsewhere.
- The journal button that opens the Quest Items screen had an unclear stock label (German "Aus Auftrag"); it now reads a clearer term in your language.
- The in-game Abilities screen (your skills, feats and Force powers) is now accessible. Its title is read when you open it; Up / Down move between the Skills, Feats and Powers tabs and Left / Right move along the entries within a tab, with each entry's name and rank read as you go. Enter (or Shift+Up / Shift+Down) reads the focused entry's full description, the same way item and quest tooltips read elsewhere. Escape steps back one level — from an entry out to the tab row, and from there out of the screen.
- You can now take an item off directly from the equipment picker. Open a slot, arrow to the item marked as currently worn (read with "(equipped)"), and press Enter — it's removed and you hear "Equipment removed". Pressing Enter on any other item still equips it as before, so the same key both swaps and removes. No extra "empty" entry was added to the list.

<h2>v0.2.1</h2>

<h3>Stability:</h3>

- The occasional few-second freeze during menu navigation — where the game and sound would lock up for a moment and then recover — should now be fixed. The mod was doing far too much background logging work on every frame; that has been cut back massively, which removes the stall.

<h3>Audio:</h3>

- New "Hint sound volume" slider under Mod settings. Left / Right adjust the volume of every mod cue — wall, door, NPC, container, item and transition cues, the navigation beacon, collisions, combat and cycle cues — in 10% steps, from 100% down to off. Each step plays a short preview at the new level so you can hear the change. Starts at 100%.
- The mod's cues now play at full volume by default. They were previously running at about 83% of unity (they were assigned a quieter engine audio channel), so they could sit too low against ambient and footstep audio; they now ride a dedicated full-volume channel.
- The map-screen edge cue (played when the map cursor hits the edge of the explorable area) is now actually audible. It was being silenced by the map screen's audio pause; it now plays on the same channel the game keeps alive for menu clicks.
- A corridor whose arm dead-ends now says so from inside the corridor: the blind arm announces as "dead end east" instead of a bare "east" that promises a way onward. In the Peragus mining tunnels a droid pocket announced a plain east exit and the east wall got searched at length for an exit that never existed.
- Room shapes on raised floors are measured at the right height. The wall-sensing rays started at height zero — beneath every wall on any floor above ground level — so real dead ends were filtered as noise, and the guessed height could jump a storey between loads, changing the announced shapes each session. Rays now take their height from the walkmesh floor triangle under the point, exact even on ramps, and the labels are stable across loads.
- Room changes announce mid-stride. Small shapes only spoke after 1.8 seconds inside them — longer than walking across one takes — so crossings were routinely passed in silence unless you happened to stop inside at the right moment. The linger requirement is now one second: real shapes announce while you keep walking, and clipping a corner stays silent.
- A rock-riddled zone speaks as an "area" now, not a "junction". A 46-metre web of paths between rocks on Lehon's southern beach merged into one cluster that announced as a junction and re-announced on every re-entry from a side channel — a maze of phantom crossings assembled from one real place. Junction webs wider than 15 metres speak as "Area. Exits: ..." like other zones; "junction" is reserved for crossings a few steps wide.
- Exits of a large area say when they sit at its far end — "west, far to the north" for a west exit you can only reach at the area's north corner. The qualifier is added only when the direction word alone would mislead: an exit that sits where its word points ("south-west" in the far south-west corner) stays unqualified, and when in doubt nothing is added.
- The facing key (Right Alt) leads with "Facing" before the compass word and pauses before the room description, so your heading and the room shape's direction words no longer run together into one chain of compass words.

<h3>Mod settings:</h3>

- Your Mod settings choices now persist between sessions. Until now every option — extended cycling, room shape descriptions, wall sounds, human-speaker subtitles, and the new hint-sound volume — silently reset to its default on each launch. They're now saved to `acc_settings.ini` in the game folder and restored on startup. (Skipping intro movies already persisted, because it's stored as the actual renamed movie files rather than as a setting.)

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

- Intro logo movies (BioWare / LucasArts / legal) are now skipped on launch by default. Eliminates 10-20 s of intro playback on cold start and avoids the engine bug where Alt+Tab during the intros restarts the queue. Toggleable at runtime under Mod settings → "Skip launch intro movies"; change applies on next launch.
- Installer UI now available in French, Italian, and Spanish. Translations are AI-drafted (German remains the human-authored quality bar); flagged in `known-issues.md` for native-speaker refinement.
- Bundled `dinput8.dll` proxy refreshed to the latest loader build.

<h3>Startup:</h3>

- A "Game is still loading, please wait." hint is now spoken if you press arrow / Enter / Space while the post-intro main menu is still loading. After 15 s of continued pressing, a second cue tells you about the Alt+F4 → cancel-dialog workaround for the known engine stall in the main-menu input pump.
- Main-menu title now reads as "Main menu" instead of leaking whichever DLC-notice label the engine had focused first.

<h3>Action menus:</h3>

- Shift+Up / Shift+Down on the target-action menu (Shift+1..3) and the action radial now read feat and force-power descriptions in addition to items. Plain verb actions (Attack, Open Door, ...) fall through to "No description available".
- Shift+Up / Shift+Down on the personal action bar (Shift+4..7) now read the full item property description instead of three bytes of CP1252 garbage. The engine never populates the tooltip slot we were reading; resolver now goes through the descriptor's tagged `action_id`.
- Shift+Enter on objects whose radial has no extra options (already-open doors, NPCs you can only talk to, ...) now speaks "No actions available for X. Press Enter to activate." instead of the bare "Action menu, X" that left the user wondering what to do next.

<h3>Dialog:</h3>

- First NPC line in a conversation is no longer occasionally double-spoken. The generic first-sight title walk was speaking the dialog panel's first label child — which IS the NPC line — and slipped past the existing suppression. Dialog and bark panels are now skipped by the title walk.

<h3>Game state:</h3>

- Pressing Pause (default Space) in-world now speaks "Paused." when paused and "Unpaused." when resumed, so you hear the state change without watching the screen. Menu opens, popup closes, and our own audio-resync cleanup are suppressed so the cue doesn't fire on top of menu narration. Engine autopauses (combat, dialog, mine-sighted, etc.) use other pause sources that aren't mapped yet and stay silent for now. Support logs also gain one `Pause: fire ...` line per engine `SetPauseState` call with caller address + mask + on/off direction, so future pause-state regressions are traceable from a single log without rebuilds.

<h3>In-world navigation:</h3>

- New hotkeys Ctrl+`,` and Ctrl+`.` jump straight to the first (closest) and last (farthest) item in the current cycling category, instead of stepping one item at a time to reach an end.

<h3>Audio:</h3>

- The Audio glossary (Mod settings → Audio glossary) now plays its cue previews from the in-game menu too, not just the title-screen options. Arrowing through the list auditions the focused cue; previously every preview was silent once a save was loaded because the in-game menu's pause muted it. The preview now rides the same priority channel as the engine's own GUI sounds, which stays audible through that pause.

<h3>Bug fixes:</h3>

- Shops now announce trade results correctly. Buying or selling a stacked or multi-stock item (e.g. one of several medpacs, or a merchant row with stock > 1) used to say "Cannot be bought / Cannot be sold" even though the trade went through — the result was inferred from the item-list row count, which only changes when a stack hits zero. Outcome is now read from your credit balance, which always moves by the price. A second, intermittent case where some items (computer spikes, repair parts) silently refused to buy is also fixed: the engine's buy/sell handler ignores rows that aren't flagged active, which keyboard navigation didn't always set.
- F5 in-game auto-update no longer fails with "Update download failed" on every press. The patch DLL was looking for the pre-rename installer EXE in the GitHub release JSON, so the asset lookup always missed and the download bailed before it started. Existing 0.1.1 users will need to manually re-run the installer once to pick up this fix; their broken DLL can't fetch a working replacement via F5. `release.ps1` now preflight-asserts consumer-side filenames and version strings against what it's about to publish, so this drift class fails at release time instead of in users' hands.
- Shift+Up / Shift+Down inside the equipment-screen item picker now reads the focused item's description. The peek path was matching the originating slot button's control id and treating the slot as empty (the engine moves the equipped item out of the slot while the picker is open, so the cached handle reads as "no item"), so the read silently no-op'd before consulting the picker listbox. Slot path now gates on "picker not armed".

## v0.1.1

**Installer:**

- Auto-launch at the end of install now honours the user's chosen install path. The previous behaviour fired `steam://run/32370` unconditionally, which made Steam launch whatever copy Steam had registered for KOTOR — fine for the default Steam install, but wrong for GoG copies, CD re-packs, manually-relocated Steam folders, or any user-specified custom path (Steam would silently launch the wrong copy or no copy at all, so the user's freshly-patched install never ran). The installer now checks whether the chosen path matches Steam's registered install for App ID 32370; if it does, the steam:// route still runs (preserves Steam overlay + cloud saves + a non-elevated launch); otherwise it launches `swkotor.exe` directly from the configured path.
- Mod-selection screen now shows only the K1CP toggle; the restored-cut-content and companion/swoop-upgrade toggles were no-ops (no installer wired up yet) and have been commented out until those installers land.
- "Collect logs" bundle no longer balloons to hundreds of megabytes. Windows Error Reporting was registered to capture full swkotor.exe process dumps, which routinely came out at 500 MB+ because the engine maps a lot of texture, audio, and BIF data we never read during triage. The installer now configures WER with a targeted custom flag set (data segments, indirectly-referenced heap pages, per-thread state, and the unpacked code segment), which preserves everything our `kdev analyze-dump` workflow relies on — stack walks, registers, heap pages pointed at from the stack (so freed-slot forensics like the recent save-popup use-after-free still work), and runtime-decrypted instructions — while dropping the asset memory that made up most of the size. Typical dump size is now ~15-50 MB instead of ~500 MB+. Note: the new flag set takes effect once you re-run the 0.2 installer or click "Collect logs" once (both refresh the WER registry entry). Crash dump files already sitting in `%LOCALAPPDATA%\CrashDumps` from earlier crashes are still in the old large format until WER captures a new crash with the updated flags.

**Dialog:**

- Human-speaker subtitles are no longer read aloud by default. KOTOR 1 ships full English / German voiceover for every human-voiced NPC, so the screen-reader was repeating the same line on top of the voice actor in a different cadence — a constant clash that made conversations harder to follow, not easier. Non-human speakers (Twi'lek, Ithorian, Rodian, Selkath, Wookiee, droids, etc.) continue to read in full, because their voiceover is the alien language and the subtitle is the only comprehensible channel. Classification is by speaker species via `appearance.2da`; the conservative call (Twi'lek, Cathar, Mandalorian, etc. all keep TTS on) means alien lines are never accidentally suppressed. If you want the old behavior — TTS reads every subtitle including humans — enable "Read human-speaker subtitles" in Mod settings.

**Bug fixes:**

- Dialog text no longer read twice by parallel speech paths. The dedicated dialog speech module and the generic panel content monitor were both announcing the NPC line each tick — fine in v0.1 because they used the same backend, but as soon as the new human-suppression toggle landed only one of them respected it, so suppressed lines still slipped through the monitor and reached the screen-reader unchanged. Dialog and bark panels are now owned by the dialog module alone.
- Combat log and container loot panels no longer announce each row twice. The dedicated per-row navigation path (Up / Down moves through the list, focused row read once) was running in parallel with the same generic content monitor that hit the dialog bug above. Same fix shape: those two panels now have a single owner.

**Menus:**

- Several menus now read more cleanly on open. The character sheet no longer dumps the full stat block, the skills and inventory screens no longer rattle off every row as one long line, and the equipment screen no longer reads its stat sidebar back at you. Each of those screens now announces only its name on open; arrow navigation reveals contents row by row, and existing self-status hotkeys still surface the same information on demand.
- Tooltip and message-box / tutorial text no longer truncated at ~256/1024 characters; long descriptions and atmospheric text now read in full.
- Game no longer crashes ~8 seconds after the main menu loads on systems without a connected mouse. Latent BioWare engine bug: when DirectInput mouse init fails, the engine releases its DirectInput interface but reports success, causing the next per-frame mouse poll to dereference NULL. Guarded at `CExoRawInputInternal::InitializeDirectInputMouse`; keyboard input is unaffected.
- Main menu reliably accepts keyboard input on first focus after launch. The earlier guard for the no-mouse crash, installed via the standard hook wrapper at the DirectInput-mouse init function entry, was interfering with the engine's foreground-cooperative-level handshake on cold start and forcing an alt-tab to wake the menu. The guard now installs inline (no wrapper) and only after the engine's first successful mouse init, so the cold-start path runs untouched and the second-call NULL-deref protection is still in place.
- Game no longer crashes a few seconds after saving. The engine frees the SaveLoad panel synchronously when the save commits, but our tab-cluster detector was still holding the freed pointer from the previous focus event and crashed when the heap allocator reused those bytes for combat-log strings. Now panels[]-validated before deref, matching the existing pattern for chain/tab panel guards.
- Shift+S no longer crashes the game. The key was wired to an experimental "selected character stat block" readout that called engine stat accessors with unvalidated addresses, occasionally smashing the stack canary and triggering an uncatchable fast-fail. The feature was never reliable in practice (the bare H self-status hotkey covers the same ground), so it has been removed entirely.
- Options-screen tabs (Gameplay / Feedback / Auto-Pause / Grafik / Sound) now activate the entry you actually focused. Previously Enter on a tab — and arrow stepping across tabs — landed one row above the intended one, so picking "Sound" opened "Grafik", picking "Feedback" opened "Gameplay", and so on. Caused by a race between the chain rebuild and the tabbed-panel detection: the engine's hit-test shift compensation never had a chance to populate before the click fired. The compensation is now derived on-demand at warp/click time instead of being cached at rebuild time.
- Shift+L (open level-up screen) no longer stacks panel copies on key-repeat. Pressing Shift+L while the level-up screen was already open allocated a fresh panel on every press, leaving the user trapped: the screen was only partially drawn, Esc only popped one of the many duplicates at a time, and the engine's Alt+F4 quit confirmation could never reach the foreground. Subsequent presses while the level-up screen is open now announce "Level Up already open" and do nothing.
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
