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

Write examples in **English**, not the German in-game labels. The release
notes are English-only and most readers are English-speaking, so quote UI
elements in English — "Force Powers" not "Machtkräfte", "Strength" not
"Stärke", "in the party" not "im Team". (The mod itself still speaks the
player's installed language in-game; this only governs how we *describe* it
here.) Where naming the exact spoken string matters, give the English term
and add the German in parentheses if it genuinely aids clarity.

<h2>Unreleased</h2>

<h3>Bug fixes:</h3>

- The action menu now steps aside for pop-up boxes and other screens instead of fighting them. When a message box (such as the quit confirmation) or another menu came up while the action menu was open, both reacted to the same arrow and Enter presses — so navigating the box also moved the hidden action menu, and Escape closed the menu rather than the box. The action menu now pauses itself while a pop-up or menu is in front of it, leaving those keys to the box, and when you close the box it returns to exactly the category and entry you were on — the same way the game's own menus come back after a pop-up.

<h2>v0.4.4</h2>

<h3>Bug fixes:</h3>

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
