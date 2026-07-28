# pazaak.cpp (884 lines)

Full accessibility driver for the Pazaak minigame board (no engine detour —
structural identification + per-tick polling per the hook-vs-poll principle).
Identifies the live `CSWGuiPazaakGame` panel by probing deep field ranges
(`LooksLikePazaak`), tracks it by pointer, disables the engine's visual
tutorial (its help popups would stack on top of the board and freeze the
turn engine), reads the `CSWPazaak`/`CPazaakPlayer` model each tick, and
announces state deltas (draws, plays, stands, set/match results). Drives
play by calling the engine's own `HandlePlayHandCard`/`HandleStand`/
`HandleContinue` handlers directly. Implements an arrow-zone navigator
(zone 0 hand / 1 your board / 2 opponent board / 3 actions) routed through
the manager input hook (`TryHandleInput`), plus a separate wager-popup
Left/Right hold-to-repeat stepper mirroring the SpeedButton auto-repeat.
Talks to `engine_manager`, `engine_panels`, `hotkeys`, `strings`, `prism`.
See `docs/pazaak-investigation.md` for the full RE.

## Declarations (in source order)

- L53-58 — engine surface typedefs + addresses: `PFN_GetTotal`, `PFN_HandleCtrl`, `PFN_HandleInt`, `kAddrGetTotal`, `kAddrHandleContinue`, `kAddrHandleStand`, `kAddrHandlePlayHandCard`, `kAddrWagerHandleInput`
  note: HandleContinue/HandleStand guard on `param_1==0 || param_1->is_active!=0` — an explicit control MUST be passed (nullptr is fine; omitting leaves stack garbage and faults).
- L61-62 — `constexpr size_t kPanelModelOffset = 0x86d0; kPanelStateOffset = 0x86d4`
  note: CSWGuiPazaakGame->pazaak model pointer and game_state.
- L72 — `constexpr size_t kTutorialActiveOffset = 0x86b4`
  note: cleared on acquire to disable the visual tutorial's blocking help popups.
- L78-79 — `kWagerCurOffset = 0xc94; kWagerMaxOffset = 0xc98`
  note: CSWGuiWagerPopup current/max wager, a different panel from the board.
- L86-93 — `kModelPlayerOffset=0x08; kModelEnemyOffset=0x78; kModelRemainOffset=0x228`
  note: corrected from an earlier wrong 0x98/0x88/0x8c guess that read dead space/deck cards.
- L90-97 — per-player offsets: `kPlayerHandOffset, kPlayerBoardOffset, kPlayerStandOffset, kPlayerScoreOffset, kCardIndexOffset, kCardFlipOffset, kCardStride`
- L103-106 — game_state constants `kStatePlayerInteractive=3, kStatePlayerPlayed=4, kStateResult=9, kStateResultWait=10`
- L109-125 — `bool ReadIntAt/ReadPtrAt(...); void WriteIntAt(...)`
  note: SEH-guarded primitive reads/writes shared by the rest of the file.
- L127-140 — `void* GetModel(void* panel); int GetState(void* panel); void* PlayerOf/EnemyOf(void* model)`
- L142 — `struct CardView { int index; int flip; }`
- L144 — `CardView ReadCard(void* player, size_t baseOff, int slot)`
- L152 — `int CallGetTotal(void* player)`
  note: SEH-wrapped call into CPazaakPlayer::GetTotal.
- L159-168 — `int BoardCount(void* player); CardView BoardLast(void* player, int count)`
- L174 — `bool LooksLikePazaak(void* panel)`
  note: structural identity probe; all reads SEH-guarded, range-checks on score/remaining/hand-index make a false positive from adjacent heap effectively impossible.
- L198 — `void FormatCard(int index, int flip, bool inHand, char* out, size_t n)`
  note: thin wrapper over the shared FormatCardLabel (declared in pazaak.h).
- L203 — `void Say(const char* s, bool interrupt=false)`
- L208 — `void BuildBoard(void* player, char* out, size_t n)`
- L222 — `void SpeakHand(void* player)`
- L241 — `void SpeakTable(void* model)`
- L255-278 — `struct Snap {...}; Snap g_prev; void* g_panel; uintptr_t g_learnedVtable; bool g_started, g_resultAnnounced, g_boardForeground`
- L274-278 — cursor state: `int g_zone, g_col; bool g_optMode; int g_optSlot, g_optSign`
  note: arrow-zone model shared with the deck builder; optMode is the +/- card sign sub-zone.
- L280 — `Snap ReadSnap(void* panel)`
- L299 — `void AnnounceDeltas(void* panel, const Snap& cur)`
  note: gates main-deck draw announcement to index>=18 since side-card plays (0..17) speak their own confirmation via the Play handler; result latch (g_resultAnnounced) fires once per set.
- L378-389 — `void DoPlay/DoStand/DoEndTurn(void* panel, ...)`
  note: thiscall dispatch wrappers, this=panel, SEH-guarded.
- L395 — `void PlayAndAnnounce(void* panel, void* player, int slot)`
- L408 — `int ZoneLen(void* model, int zone)`
- L418-431 — `bool HandSlotFilled(...); int FirstHandCol(...); int NextHandCol(...)`
  note: hand-zone Left/Right skip empty slots, no wrap.
- L435 — `void SpeakBoardSide(void* model, bool playerSide)`
- L447 — `void AnnounceZoneEntry(void* model, int zone)`
- L456 — `void AnnounceZoneElement(void* model, int zone, int col)`
- L480 — `void ConsumeConflicts()`
  note: Consume() works because pazaak::Tick runs inside the same BeginTick/EndTick window as the in-world/menu pollers (Claim-vs-Consume rule).
- L497 — `void PollShortcuts(void* panel)`
  note: letter shortcuts (s/e/c/t, Shift+C) are Win32-polled since the engine drops those scancodes before the manager hook; arrows+Enter go through TryHandleInput instead.
- L536 — `void HandleEnter(void* panel, void* model, int state)`
  note: hand index 12..17 (+/- cards) enters the sign sub-zone before playing.
- L569-589 — wager stepper state: `void* g_wagerPanel; int g_wagerLast, g_wagerHeldDir; unsigned g_wagerHoldStart, g_wagerNextStep; bool g_wagerRepeating` + tuning constants `kWagerHoldDelayMs=350, kWagerRepeatSlowMs=140, kWagerRepeatFastMs=30, kWagerRampMs=1200`
- L591 — `void StepWager(void* fg, int dir)`
- L597 — `void ServiceWagerPopup(void* fg)`
  note: Option A — silent during the hold-to-repeat race, announces the final value on key release.
- L665 — `void ResetState()`
- L682 — `void FormatCardLabel(int index, int flip, CardContext ctx, char* out, size_t n)`
  note: exported (declared in pazaak.h); shared by the board game and the side-deck builder (menus_pazaakdeck).
- L714 — `bool TryHandleInput(void* activePanel, int param_1, int param_2, int& rv)`
  note: manager-hook entry; card-options sub-zone (g_optMode) owns Left/Right/Enter/Esc first, then zone nav, then Enter dispatch.
- L799 — `bool IsBoardForeground()`
- L801 — `void DispatchWagerInput(void* panel, int code)`
  note: calls CSWGuiWagerPopup::HandleInputEvent(panel, code, 1) directly, same path as the engine's own button-push callbacks.
- L814 — `void Tick()`
  note: per-frame driver — wager-popup service, panel drop/acquire, snapshot+delta announce, then input polling only when board is foreground.
