# Pazaak Accessibility — Investigation & Concept

Status: **board game implemented (`patches/Accessibility/pazaak.cpp`), pending in-game
test; pre-game start/deck-build menu (§11) still TODO.** This doc captures the full
reverse-engineering of KOTOR 1's Pazaak minigame plus the resulting accessibility concept.

Implementation notes (board game, 2026-06-01):
- No engine detour hook. `acc::pazaak::Tick()` (wired into `core_tick::Dispatch` ahead of
  the in-world/menu pollers) identifies the live `CSWGuiPazaakGame` structurally — the
  foreground modal whose deep fields (`+0x86d0` model ptr, `+0x86d4` game_state, plus
  model score/remaining/hand-index range checks) match the layout — then tracks it by
  pointer until it leaves the manager. The panel vtable is learned + logged on first sight
  (`Pazaak: acquired board panel=... vtable=0x...`) so it can be promoted to a hard
  constant later if desired.
- Reads the model each tick and announces deltas (draws, opponent actions, stands,
  set/match win/lose/tie). Drives play via the engine handlers `HandlePlayHandCard`/
  `HandleStand`/`HandleContinue` called directly off the tick (safe deferred site). ± cards:
  the hand card's `is_flipped` is written directly before `HandlePlayHandCard` (mirrors the
  engine's HandleFlipHandCard-then-play flow).
- Keys are Win32-polled via the hotkey registry (new `Pazaak*` actions), gated on the board
  being foreground; the tick `Consume()`s the in-world/menu actions sharing Tab/Enter/
  arrows/Esc so they don't double-fire.

All RE here comes from Lane's Ghidra DB (`docs/llm-docs/re/`) — decompiled with
`tools/ghidra-scripts/Decompile.java`, struct layouts from `swkotor.exe.h`, the card
value table dumped with `DumpBytes.java`. Card semantics and the turn state-machine were
read directly from engine pseudocode; they are not guessed.

Prior context: the third-party "Auto-Pazaak" mod (`third_party/Auto-Pazaak.zip`) does
**not** touch the minigame — it only flips quest globals and calls `GiveGoldToCreature`.
No reuse value. We build the whole thing ourselves.

---

## 1. Goal

A blind player must be able to play Pazaak end-to-end with screen reader + keyboard only:
read both players' boards and their own hand, hear what each player does and the result of
each set/match, understand every card, and play cards / stand / end turn correctly —
including declaring a +/- card's sign before playing it.

---

## 2. Official rules (reference)

The object is to have your face-up cards total higher than your opponent's without
exceeding 20. A total over 20 at the **end of your turn** is a BUST and loses the set.
First to win 3 sets wins the match and the wager.

Equipment:
- Main deck: 40 cards, four each of 1..10. Shared dealing deck; each player draws from it.
- Side deck: up to 10 cards the player owns, values -6..+6 (and +/- flip cards).

Play:
1. Each player draws 4 random cards from their side deck to form their HAND. The hand
   lasts the whole match; each hand card is playable once per match.
2. On your turn you automatically draw one main-deck card face-up.
3. After drawing you may optionally play **one** hand card.
4. Only one hand card per turn.
5. Then you choose STAND or END TURN. Standing locks your total for the rest of the set
   (no more draws/cards). When one player stands, the other keeps taking turns until they
   stand or bust.
6. END TURN means you'll auto-draw again next turn.
7. Turns alternate until both stand, or someone ends a turn over 20 (bust). You only lose
   to a bust if you're over 20 *at the end of your turn* — you can draw over 20 then play a
   negative hand card to come back under.
8. A tie set is replayed; no new hand cards are drawn.
9. Max nine cards in play per set (hand + dealt). The ninth card auto-stands you.
10. A +/- card must be declared positive or negative when played; can't change after.
11. First to 3 sets wins the match and wager.

Initial side deck is weak (two each of +1..+5); the player should acquire better cards.

---

## 3. Engine architecture overview

Two GUI panels and a headless game model:

- **`CSWPazaak`** — the game-logic model (no UI). Holds both players, the main deck, the
  draw cursor, and the AI state. This is the single source of truth we read.
- **`CSWGuiPazaakStart`** — the pre-game screen: build your 10-card side deck from your
  collection, then set the wager (via `CSWGuiWagerPopup`). Loads layout `pazaakdeck` /
  wager popup. On accept it constructs the game panel.
- **`CSWGuiPazaakGame`** — the actual board. Loads layout **`pazaakgame`** (i.e.
  `pazaakgame.gui`). Owns the per-slot card controls, the action buttons, the result
  message box, and a `CSWPazaak*` (`->pazaak`). This is the primary accessibility target.

### Launch lifecycle

1. NWScript `PlayPazaak(oppObject, deckNum, endScript, maxWager, showTutorial)` (script
   command 364) → `CSWVirtualMachineCommands::ExecuteCommandPlayPazaak` @0x00540fd0.
2. → `CClientExoApp::StartPazaakGame` @0x005eddc0 → `CClientExoAppInternal::StartPazaakGame`
   @0x005f3810. Sets `CPazaakData` (game_running=1, wager clamped to player gold,
   end_script, opponent deck), calls `SetSoundMode(4)` (pauses world audio — see §11),
   then `new CSWGuiPazaakStart(...)` and adds it as a modal panel.
3. Player builds side deck + sets wager. `CSWGuiPazaakStart::HandleStartDialog` @0x00681890
   saves the chosen deck to the party table, calls `ChooseSidedeck`, pops the start panel,
   then **`new CSWGuiPazaakGame(manager, pazaak, showTutorial, opponentId)`** @0x006808a0
   and adds it as a modal panel. ← this is where the board instance is born.
4. Game runs; on match end `DoGameSequence` state 0xc calls
   `CClientExoApp::EndPazaakGame` with whether the player won, plus the end script.

---

## 4. Data model (read these at runtime)

### `CPazaakCard` (8 bytes)
- `+0x0 index` — card identity (see §5). -1 = empty slot, -2 = face-down (hidden enemy).
- `+0x4 is_flipped` — for +/- cards, 1 means currently showing the minus face.

### `CPazaakPlayer`
- `+0x00 hand_cards[4]` — the 4 side-deck cards drawn into hand (CPazaakCard each).
- `+0x20 board_cards[9]` — cards laid on the table this set (fills left to right; first
  `index==-1` is the end).
- `+0x88 stand` (int) — non-zero once this player has stood.
- `+0x8c score` (int) — sets won this match (0..3).

### `CSWPazaak` (the model)
- `+0x0 field0_0x0` — used as a flag passed to EndPazaakGame.
- `+0x4 opponent_id` — server object id of the opponent creature.
- `+0x8 player` (CPazaakPlayer) — the human player.
- `+0x98 enemy` (CPazaakPlayer) — the AI opponent.
- `+0x128 cards[40]` (CPazaakCard) — the shuffled main deck.
- `+0x228 remaining_card_count` — draw cursor; DrawCard reads `cards[count]` then
  decrements; <0 triggers reshuffle.
- `+0x22c field6_0x22c` — the AI turn sub-state (see PlayAITurn).

Get the model at runtime from the board panel: `CSWGuiPazaakGame->pazaak` (field at
`+0x86d0`).

---

## 5. Card encoding (the key to everything)

`index` selects both a numeric value (`pazaakCardValues` @0x007a26b8, 28 ints) and a label.
Dumped values, by index:

- 0..5  → +1, +2, +3, +4, +5, +6   (side-deck PLUS cards)
- 6..11 → -1, -2, -3, -4, -5, -6   (side-deck MINUS cards; stored negative in the table)
- 12..17 → 1, 2, 3, 4, 5, 6        (side-deck +/- FLIP cards; magnitude only)
- 18..27 → 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 (MAIN-deck cards)

So the 18 side-deck card *types* are indices 0..17 (6 plus + 6 minus + 6 flip). This is
exactly `pt_pazaakcards[18]` (collection counts) and the `all_cards[18]` GUI grid in the
start menu. Main-deck cards (18..27) are only ever dealt automatically.

Scoring contribution of a board card (`CPazaakPlayer::GetTotal` @0x006e4360):
- index == -1 → end of board, stop.
- index < 0 → contributes 0.
- index in 12..17 **and** is_flipped != 0 → contributes **-value** (the minus face).
- otherwise → contributes `pazaakCardValues[index]` as-is.

Our human-readable card naming (synthesize ourselves; do not depend on engine strings):
- 0..5  → "plus N"  (N = index + 1)
- 6..11 → "minus N" (N = index - 5)
- 12..17 → flip card, magnitude M = index - 11. In hand, announce both faces and the
  current one, e.g. "plus or minus 3, currently plus 3" (currently = is_flipped ? minus : plus).
  On the board it already has a committed sign → "plus 3" or "minus 3".
- 18..27 → "N" (N = index - 17)  (a plain main-deck card)
- -1 → empty
- -2 → face-down

---

## 6. Scoring & winner

`CSWPazaak::DetermineWinner` @0x006e48a0 returns a char:
- 0 → set not over (at least one player still hasn't stood and nobody is forced).
- 1 → player wins the set.
- 2 → enemy wins the set.
- 3 → tie (replay set, keep hands).

Logic: if either total > 20, both are forced to stand. Once both stand, a >20 total is
treated as -1,000,000 (a bust always loses). Higher total wins; equal → tie.

`CPazaakPlayer::AddGameCard` @0x006e45f0 auto-sets `stand` when the board reaches 9 cards
**or** the total hits exactly 20.

---

## 7. Turn state machine — `CSWGuiPazaakGame::DoGameSequence` @0x00680030

The master phase lives at `CSWGuiPazaakGame +0x86d4` (call it `game_state`). `DoGameSequence`
is pumped each frame (via Draw). States:

- 0 → init, advance to 1.
- 1 → start player turn. If player already stood → 5. Else light player turn indicator,
  play `mgs_startturn`, go to 2.
- 2 → `DrawPlayerCard` (auto-draw one main card onto player board), play `mgs_drawmain`;
  if total > 20 play `mgs_warnbust`; go to 3.
- 3 → **player's interactive turn** (can play a hand card, then end turn or stand).
- 4 → player has played a hand card this turn; can now only end turn or stand (rule 4).
- 5 → player turn finished. Tutorial nags at 20 / bust. DetermineWinner; if decided → 8,
  else → 6.
- 6 → start enemy turn (if enemy stood → 8). Light NPC indicator, play `mgs_startturn`,
  go to 7.
- 7 → run one AI action via `FUN_0067cb10` → `PlayAITurn`. Return code drives sound/colour:
  1 = AI drew a main card, 2 = AI played a hand card (`mgs_playside`), 3 = AI stood,
  4 = AI ended turn, 0 = AI turn fully done → resolve.
- 8 → DetermineWinner. 0 → loop back to 1. Otherwise bump the winner's `score`, pick a
  result sound (`mgs_winset` / `mgs_winmatch` / `mgs_loseset` / `mgs_losematch`, tie uses a
  message only), and go to 9.
- 9 → play the result sound, attach the result message box (callback `HandleEndDialog`),
  go to 10.
- 10 → waiting on the result message box.
- 0xb → `StartNewGame` (next set) → back to 0.
- 0xc → end match: `EndPazaakGame(player won?, ...)`.

`HandleEndDialog` @0x0067e9a0: if neither player has 3 sets → state 0xb (new set); else →
state 0xc (end match).

Sounds (all play at SetSoundMode priority group **0xb**, set in the ctor):
`mgs_startturn`, `mgs_drawmain`, `mgs_playside`, `mgs_warnbust`, plus the result sounds.

---

## 8. Player action handlers (drive these for keyboard play)

All are `CSWGuiPazaakGame` methods; all guard on `game_state` and refuse if not the player's
interactive turn, so calling them directly is safe.

- **End Turn** — `HandleContinue` @0x0067ec20. Guard: player not stood, state 3 or 4. Sets
  state 5. (Wired to button `activated_buttons_[0]`, gui tag `BTN_XTEXT`.)
- **Stand** — `HandleStand` @0x0067ed00. Guard: player not stood, state 3 or 4. Sets
  `player.stand = 1`, state 5. (Button `activated_buttons_[1]`, gui tag `BTN_YTEXT`.)
- **Play hand card** — `HandlePlayHandCard(index)` @0x0067ede0. Guard: state == 3,
  0<=index<4, `CanUsePlayerSidedeckCard(index)`. Moves hand card to board, plays
  `mgs_playside`, sets state 4. (After this, no more hand cards this turn — matches rule 4.)
- **Flip a +/- card** — `HandleFlipHandCard` @0x0067dcf0 toggles `is_flipped` on the hand
  card that is the panel's `active_control`. Only valid for index 12..17. There is also
  `HandleFlipButtons(ctrl)` @0x0067dda0 that flips by hand-slot id. Use flip to set the +/-
  declaration **before** Play.
- Helper checks: `CanUsePlayerSidedeckCard(i)` @0x006e4840 (hand slot i has index>=0);
  `UsePlayerSidedeckCard(i)` @0x006e4860 (the underlying move, called by HandlePlayHandCard).

The native mouse flow uses a "hand mode" (`field31_0x86e4`: 0 normal, 1 play-pending,
2 flip-pending) toggled by `SetHandEnabled` + `HandleHandcardAction`. We can bypass all of
that and call `HandlePlayHandCard` / `HandleFlipHandCard` directly.

`activated_buttons_[2]` (`HandleXButton`) and `[3]` (`HandleBlackButton`) are gamepad-only
virtual buttons (play / flip confirm); ignore for keyboard.

Quit: `CSWGuiPazaakGame::HandleInputEvent` @0x0067e8f0 already opens a quit-confirm message
box on certain key codes (0x28 / 0x2e) — exit path already exists.

---

## 9. GUI layout & control wiring (`pazaakgame.gui`)

From the ctor @0x006808a0 (`StartLoadFromLayout "pazaakgame"`, `InitControl` per tag):

Status / labels:
- `LBL_PLRNAME`, `LBL_NPCNAME` — player / opponent name. Opponent name set from creature
  full name; player name set in `OnPanelAdded`.
- `LBL_PLRTOTAL`, `LBL_NPCTOTAL` — running totals (updated from `GetTotal`).
- `LBL_PLRSIDEDECK`, `LBL_NPCSIDEDECK` — side-deck count labels.
- `LBL_PLRTURN`, `LBL_NPCTURN` — whose-turn indicators.
- `LBL_PLRSCORE0..2`, `LBL_NPCSCORE0..2` — the set-win pips (best of 3); filled with
  `lbl_winmark02` as sets are won.
- `LBL_FLIPICON`, `LBL_FLIPLEGEND` — flip hint, shown when a +/- card is in focus.

Board card slots (9 per side), built by sprintf loop:
- Player board: `BTN_PLR0..8` + `LBL_PLR0..8` → `field11_0x11e4[9]`.
- Enemy board: `BTN_NPC0..8` + `LBL_NPC0..8` → `field13_0x3a50[9]`.

Hand card slots (4 per side) + flip buttons, second sprintf loop:
- Player hand: `BTN_PLRSIDE0..3` + `LBL_PLRSIDE0..3` → `field12_0x2de0[4]`. Each hand
  control gets events: 0x27 activate → `HandleHandcardAction`; 0x17feb drop →
  `HandleHandCardDrop`; 0x1f9 double-click → `HandleHandCardDoubleClick`; 0x44 right-click
  → `HandleHandCardRightClick`. Control `id` = slot index 0..3.
- Enemy hand: `BTN_NPCSIDE0..3` + `LBL_NPCSIDE0..3` → `field14_0x564c[4]` (shown face-down,
  index -2, unless debug flag `DAT_00833bb0` is set).
- Flip buttons: `BTN_FLIP0..3` → `field15_0x62bc[4]`; event 0x27 → `HandleFlipButtons`.

Action buttons (wired after StopLoadFromLayout):
- `BTN_XTEXT` = `activated_buttons_[0]` → event 0x27 → `HandleContinue` (End Turn).
- `BTN_YTEXT` = `activated_buttons_[1]` → event 0x27 → `HandleStand` (Stand).
- `[2]` → `HandleXButton`, `[3]` → `HandleBlackButton` (gamepad confirm; no layout tag).

`CSWGuiPazaakCard::SetCard(index, is_flipped)` @0x0067cd30 sets each card control's label
text and fill image (`lbl_cardmpos` / `lbl_cardmneg` / `lbl_cardrarem` / `lbl_cardraref` /
`lbl_cardstand` / `lbl_cardback`). We don't need its strings — we synthesize labels from
index (§5).

`RefreshDisplay` @0x0067d4b0 is the engine's "repaint from model" — it re-reads the model
every time state changes and rewrites all card controls, totals, score pips, and button
enable states. **This is our read anchor** (see §10).

---

## 10. Accessibility concept

### Zones (per agreed UX)
- **Table zone** (`t`): two rows.
  - Player row: `player.board_cards[0..]` left→right until index==-1.
  - Opponent row: `enemy.board_cards[0..]` left→right until index==-1.
  - Reading a board card: its committed value via §5 (flip cards already have a sign).
- **Hand zone** (`c`): one row, `player.hand_cards[0..3]` left→right. Empty/played slots
  (index==-1) are skipped or announced as "empty". Flip cards announce both faces.

### Keyboard model (per agreed UX)
- **Tab / Shift+Tab** — cycle **only the playable hand cards** (hand slots with index>=0).
  End Turn and Stand are NOT in the Tab cycle — they live solely on `e` / `s`. In state 4
  (a card has already been played this turn) there are no more playable cards, so Tab has
  nothing to cycle. Announce each card on focus (title-style, not a full enumeration).
- **Enter** — play the focused hand card: plain card → `HandlePlayHandCard(slot)`; ± card →
  open the card-options sub-zone (see "Playing a card" below).
- **s** — Stand (`HandleStand`), regardless of focus.
- **e** — End Turn (`HandleContinue`), regardless of focus.
- **c** — jump to / read the hand zone (review hand cards).
- **t** — jump to / read the table zone (review both boards + both totals).
- **Playing a card**: Enter on a focused hand card.
  - Plain card (index 0..11) → play immediately via `HandlePlayHandCard(slot)`.
  - +/- flip card (index 12..17) → Enter opens a **card-options sub-zone**. Left/Right move
    between the two options ("plus N" / "minus N"); Enter on an option sets the card's sign
    to match (via `is_flipped` / `HandleFlipHandCard`) then plays it with `HandlePlayHandCard`;
    **Escape** closes the sub-zone without playing (card stays in hand, focus returns to it).
    This is the rule-10 declaration. Note the default starting side deck has no ± cards, so
    this sub-zone only appears once the player acquires flip cards.

### Automatic announcements (event-driven, from the RefreshDisplay hook + state)
- Player draws (state 2→3): "You drew {card}. Your total {N}." Warn on bust.
- Your turn begins: prompt the available options briefly.
- Opponent actions (state 7 codes): "{Opponent} drew a card / played {card} / stands /
  ends turn." Announce opponent total when it changes (note: opponent hand cards stay
  hidden; only board cards are public, which matches sighted play).
- Set/match result (state 8/9): "You win the set ({your sets}-{their sets})" etc.,
  "You win the match!" / "You lose the match." Tie → "Tie, replaying set."

### Runtime instance capture
The board panel is constructed in `HandleStartDialog` @0x00681890. Capture the live
`CSWGuiPazaakGame*` by hooking either the ctor @0x006808a0 (grab `this` on return) or
`OnPanelAdded` @0x0067ffa0. Clear it when the panel is destroyed (`~CSWGuiPazaakGame`
@0x0067e540 / @0x0067ff80) or on EndPazaakGame.

### Read & input hook strategy
- **Read**: hook `RefreshDisplay` @0x0067d4b0 (post), read `this->pazaak`, diff against our
  cached snapshot, and announce deltas. This fires on every meaningful change, so no polling
  needed. (Consistent with our hook-for-control-flow / read-on-event principle.)
- **Input**: intercept our hotkeys while the Pazaak board is the foreground panel. Preferred
  seam is `CSWGuiPazaakGame::HandleInputEvent` @0x0067e8f0 (claim s/e/c/t/Tab/Enter before
  the panel's default handling), or the manager-level input hook gated on "foreground panel
  vtable == `CSWGuiPazaakGame_vtable`". Use the established Claim-not-Consume rule for
  suppressing engine handling of bound keys.

### Audio / pause note
`StartPazaakGame` calls `SetSoundMode(4)`, which pauses world sources except priority groups
1/2/0xb. The engine's own Pazaak cues use group 0xb. Our speech goes through Prism/Tolk
(unaffected), but any custom audio cues we add must use priority group **0xb** to stay
audible, exactly like the glossary fix (see memory `project_sound_mode_priority_group_pause`).

---

## 11. The pre-game Start menu (`CSWGuiPazaakStart`) — second implementation phase

Less detailed here; needs its own pass. Known surface:
- Builds the 10-card side deck from the collection. `all_cards[18]` = the grid of owned
  card types (counts in `card_counts[18]`); `sidedeck_gui[10]` = chosen slots; model
  `sidedeck[10]`.
- Key handlers: `AddChosenCard` @0x0067fb10, `RemoveChosenCard` @0x0067fd10, `HandleMoveCard`
  @0x006807e0, `HandleAcceptButton` @0x006819e0, `HandleStartDialog` @0x00681890 (commits and
  launches the game).
- Wager via `CSWGuiWagerPopup`: `less_button` / `more_button` (`CSWGuiSpeedButton`),
  `wager_value_label`, `maximum_label`, `wager_button`, `quit_button`.
- Persistence: on accept, the 10 chosen indices are written to party table
  `pt_paz_sidedeck[10]`; the collection is `pt_pazaakcards[18]`.

The board game (§3–§10) is the priority; the start menu can reuse our select-then-confirm
listbox/grid patterns once the board plays.

---

## 12. Persistence (party table, `CSWPartyTable`)
- `pt_pazaakcards[18]` — how many of each side-deck card type the player owns (indices 0..17).
- `pt_paz_sidedeck[10]` — the player's currently chosen 10-card side deck (card indices).

`CPazaakData` (in `CClientExoAppInternal`): `game_running`, `end_script`, `wager`,
`won_last_game` (read by `GetLastPazaakResult`, script command 365), `opponent_pazaak_deck`.

---

## 13. Address reference (for hooks.toml / implementation)

Model (`CSWPazaak` / `CPazaakPlayer`):
- `CPazaakPlayer::GetTotal` 0x006e4360
- `CPazaakPlayer::AddGameCard` 0x006e45f0
- `CPazaakPlayer::ChooseSidedeck` 0x006e4580
- `CSWPazaak::DrawCard` 0x006e4800
- `CSWPazaak::DrawPlayerCard` 0x0067cae0
- `CSWPazaak::ShuffleDeck` 0x006e4730
- `CSWPazaak::ClearGameBoard` 0x006e4640
- `CSWPazaak::DetermineWinner` 0x006e48a0
- `CSWPazaak::CanUsePlayerSidedeckCard` 0x006e4840
- `CSWPazaak::UsePlayerSidedeckCard` 0x006e4860
- `CSWPazaak::PlayAITurn` 0x006e4c60  (driver `FUN_0067cb10` 0x0067cb10)
- `CSWPazaak::CSWPazaak` (ctor) 0x006e4c00
- `pazaakCardValues` (28 ints) 0x007a26b8

Board panel (`CSWGuiPazaakGame`):
- ctor 0x006808a0
- `OnPanelAdded` 0x0067ffa0
- `DoGameSequence` 0x00680030
- `RefreshDisplay` 0x0067d4b0
- `Draw` 0x00680710
- `StartNewGame` 0x0067e770
- `SetHandEnabled` 0x0067dbb0
- `HandleInputEvent` 0x0067e8f0
- `HandleContinue` (End Turn) 0x0067ec20
- `HandleStand` 0x0067ed00
- `HandlePlayHandCard` 0x0067ede0
- `HandleFlipHandCard` 0x0067dcf0
- `HandleFlipButtons` 0x0067dda0
- `HandleHandcardAction` 0x00680790
- `HandleEndDialog` 0x0067e9a0
- `HandleHandCardDoubleClick` 0x0067ef80
- `HandleHandCardRightClick` 0x0067dd80
- `~CSWGuiPazaakGame` 0x0067e540 (and 0x0067ff80)
- `CSWGuiPazaakCard::SetCard` 0x0067cd30

Launch / end:
- `ExecuteCommandPlayPazaak` 0x00540fd0
- `ExecuteCommandGetLastPazaakResult` 0x0053b1e0
- `CClientExoApp::StartPazaakGame` 0x005eddc0
- `CClientExoAppInternal::StartPazaakGame` 0x005f3810
- `CSWGuiPazaakStart::HandleStartDialog` 0x00681890

Struct sizes: `CSWGuiPazaakGame` 0x86e8, `CSWGuiPazaakStart` 0x8928.

Field offsets on `CSWGuiPazaakGame`: `pazaak` +0x86d0, `game_state` (field27) +0x86d4,
`field28` (timer) +0x86d8, `field29` (tutorial-shown bits) +0x86dc, `field30` +0x86e0,
`field31` (hand mode) +0x86e4. Card-control arrays: player board +0x11e4, player hand
+0x2de0, enemy board +0x3a50, enemy hand +0x564c, flip buttons +0x62bc.

---

## 14. Design decisions

Settled (2026-06-01):

- **Scope** — build the board game **and** the side-deck/wager start menu together in the
  first pass (not board-only). Expand §11 into a full design before implementing.
- **+/- declaration UX** — Enter on a ± hand card opens a card-options sub-zone (Left/Right
  between "plus N"/"minus N", Enter plays with that sign, Escape cancels). See §10.
- **Card labels** — synthesize from the index (§5); do not use the engine's cosmetic strings.
- **Instance capture** — implementation detail; decide ctor @0x006808a0 vs OnPanelAdded
  @0x0067ffa0 + IsPanelLive teardown guard at implementation time.

- **Tab cycle** — Tab cycles **only the playable hand cards**; End Turn and Stand are
  reached only via the `e` / `s` shortcuts, never via Tab. See §10.

- **Turn-start auto-announce** — on the player's turn start, speak the just-drawn card + the
  new running total + a brief "your turn" cue. Hand reviewed on demand (`c`), boards on
  demand (`t`); do NOT enumerate the full hand every turn (title+focus convention).
- **Opponent turn** — narrate each opponent action with its resulting board total (e.g.
  "drew, total 14" / "played minus 2, total 12" / "stands at 18" / "ends turn, total 15").
  Opponent hand stays hidden; board + total are public, matching sighted play. Pacing is
  gated by the engine's per-action delays.

No open decisions remain. Ready for implementation.
