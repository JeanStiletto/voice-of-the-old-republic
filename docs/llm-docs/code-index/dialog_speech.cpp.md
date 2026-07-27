# dialog_speech.cpp (765 lines)

Poll-based live dialog/bark narration: speaks the NPC line, computer-terminal listbox rows, and bark-bubble text on change; handles R-repeat; and implements the "human/droid VO subtitle suppression" system so intelligible Basic-VO lines don't overlap TTS while genuinely alien speakers (and per-tag exceptions) always get spoken. Talks to engine_area/engine_player/engine_reads (speaker resolution), menus_modsettings (HumanSubtitles toggle), tutorial_hints/tutorial_popup (Trask rewritten-line handoff), locked_recall (story-lock bark capture), transitions (module-load gate), prism/strings.

## Declarations (in source order)

- L64-75 — `kHumanAppearanceMaxId=508`, `kHumanAppearanceMask[8]` — build-generated 512-bit appearance.2da human/non-human bitmask (see build/dump-2da/Program.cs)
  note: false-negative biased — unknown/modded appearances classify non-human (TTS stays on)
- L77 — `bool IsHumanAppearance(int appearanceType)`
- L84 — `constexpr int kRaceDroid = 5`
- L156 — `constexpr const char* kNeverSuppressTags[]` — sasha, vek, tat17_03komad_01, kas22_xkomad_01, kor35_tariga, kor35_adrenas
  note: per-tag override forcing speech ON despite human/droid classification (alien-language speakers sharing a "human" model)
- L165 — `bool IsNeverSuppressTag(const char* tag)`
- L196 — `constexpr const char* kAlwaysSuppressTags[]` — man26_casandra, man28_merc, kor35_yuthura
  note: mirror-image override forcing suppression despite non-human classification (Basic-voiced speakers sharing an alien model)
- L202 — `bool IsAlwaysSuppressTag(const char* tag)`
- L225 — `bool IsSuppressibleSpeaker(const SpeakerInfo& info)`
  note: never-suppress tag wins ties over always-suppress
- L261 — `char FoldByte(unsigned char c)` — CP1252 lowercase-ASCII fold for e-accented chars only
- L275 — `bool ExtractSkillCheckMarker(const char* line, char* out, size_t outSize)`
  note: detects leading `[Erfolg]`/`[Failure]`/etc. across EFIGS via a flat multilingual root union; lets a suppressed line's skill-check outcome still reach the player
- L106 — `struct SpeakerInfo { void* speaker; int appearance; int raceEnum; char tag[32]; }`
- L321 — `bool FillSpeakerFromServerObject(void* partner, SpeakerInfo& out)`
  note: validates kind==Creature; reads tag/race/appearance via stats pointer (inline appearance cache at +0xa4c always reads 0, so stats is mandatory)
- L358 — `uint32_t ReadGuiDialogSpeakerClientId()` — CGuiInGame+0x170
- L370 — `bool ResolveDialogSpeaker(SpeakerInfo& out)`
  note: PRIMARY = GUI per-line speaker (covers overheard NPC-NPC); FALLBACK = player's dialog_owner field (+0x54, a HANDLE not a pointer)
- L423 — `bool ResolveBarkSpeaker(void* barkPanel, SpeakerInfo& out)` — CSWGuiBarkBubble.object_id @+0x1c0
- L453 — `bool ShouldSuppressNpcLine()` — unused helper mirroring the inline Tick() logic
- L465 — `struct DialogPanelMatch { void* panel; PanelKind kind; }`
- L470 — `DialogPanelMatch FindActiveDialogPanel()` — scans gui manager panels[] for DialogCinematic(Copy)/DialogComputer(Camera)
- L499 — `void* FindBarkBubblePanel()`
- L519 — `int ReadListBoxRowCount(void* panel, size_t lbOffset)`
- L537 — `bool ReadFirstVisibleText(void* panel, char* outBuf, size_t bufSize)` — BarkBubble label fallback via FromControl walk
- L562 — `void Tick()` (public)
  note: gated on `IsModuleLoadPending`; per-line: resolves speaker, checks suppression + tutorial-hint force-spoken override, speaks marker-only when suppressed; computer-dialog variant also drains new terminal rows; R-repeat replays s_lastNpcLine verbatim even if suppressed; bark handling is an independent lifecycle honoring the same toggle, feeding locked_recall::MaybeCapture
