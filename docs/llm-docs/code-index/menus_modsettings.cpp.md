# menus_modsettings.cpp (791 lines)

Implements the entirely-virtual "Mod Einstellungen" submenu reached from a sentinel chain entry injected into InGameOptions/MainMenuOptions. Owns the sentinel pointer, per-option toggle bits (persisted via `mod_settings_store`), a nested Audio-glossary sub-submenu (delayed 750ms cue playback so speech doesn't bleed over the sample, via a self-managed `LoopSource` riding priority group 0xb to survive the in-game pause), a Slider row kind (CueVolume routed to `audio_bus`, UrgentVolume to `prism`), a Submenu row kind that pivots into either the Audio glossary or the nested `menus_keybinds` configurator, and a foreground-divergence auto-close guard (silently releases input when the engine pushes a modal like an Alt+F4 quit-confirm over the parent). Talks to `audio_bus`, `audio_cues`, `audio_loop`, `intro_skip` (SkipIntros is filesystem-backed, not a toggle bit), `menus_chain` (RebindChain on close), `menus_keybinds`, `mod_settings_store`.

## Declarations (in source order)

- L34 — `char s_rootSentinel` — never-deref'd sentinel byte; its address is the virtual chain entry's `control`
- L49 — `bool s_toggles[Option::Count]` — in-memory toggle defaults (ExtendedCycling off, RoomShapes/WallSounds on, HumanSubtitles off, TurretAutoAim off, SkipIntros/CueVolume/UrgentVolume/Keybindings/AudioGlossary unused slots)
- L97-117 — submenu state: `s_open, s_focused, s_parentPanel, s_fgAtOpen, s_glossaryOpen, s_glossaryFocused, s_keybindsOpen, s_pendingValid, s_pendingFireAt, s_pendingCue`
- L118 — `constexpr DWORD kGlossaryDelayMs = 750`
- L127 — `acc::audio::LoopSource s_glossaryPreview` — self-managed non-looping 2D source for glossary/cue-volume auditioning
- L129 — `enum class RowKind { Toggle, Submenu, Slider }`
- L141-143 — `constexpr int kCueVolumeStep = 10`, `kCueVolumePreviewGroup = 0x0b`, `kCueVolumePreviewCue`
- L149 — `int GetSliderPercent(Option)` / L157 `void SetSliderPercent(Option, percent)` — route by Option to audio_bus or prism
- L165 — `struct OptionSpec { option, label, kind }`; L171 `constexpr OptionSpec k_options[10]`
- L192 — `const char* PersistKey(Option opt)` — ini-key mapping for the 5 persisted toggles
- L206 — `void EnsureTogglesLoaded()` — pulls acc_settings.ini values once, lazily
- L223 — `struct GlossaryEntry { cue, label }`; L228 `constexpr GlossaryEntry k_glossary[19]` — NavCue → label table (Landmark cue omitted — silent by design)
- L254 — `const char* StateText(int optionIdx)` — SkipIntros reads the filesystem state live instead of s_toggles
- L281 — `void SpeakFocusedOption()` — Toggle/Slider/Submenu row formats
- L306 — `void SpeakFocusedGlossaryEntry()`
- L318 — `void CancelPendingGlossaryCue()`
- L338 — `bool ForegroundDivergedFromParent()` — baseline is s_fgAtOpen snapshot, not s_parentPanel, so the InGameOptions strip-on-top steady state doesn't false-fire
- L351 — `void* acc::menus::modsettings::GetRootAnchor()` / L355 `bool IsRootAnchor(control)`
- L359 — `void acc::menus::modsettings::ForEachRootAnchor(panel, callback, userData)` — sortCy=9000 (last chain stop), sortCx=180 (matches Options column)
- L384 — `bool acc::menus::modsettings::ExtractRootLabel(outBuf, bufSize)`
- L398 — `bool acc::menus::modsettings::IsOpen()`
- L402 — `void acc::menus::modsettings::OpenSubMenu(void* parentPanel)` — snapshots s_fgAtOpen, speaks title + first option
- L434 — `void acc::menus::modsettings::Close()` — speaks "closed", rebinds chain on parent
- L466 — `void AutoCloseSilent()` — used on foreground divergence; skips speech and parent rebind (the new panel announces itself)
- L486 — `void OpenGlossarySubMenu()` / L504 `void CloseGlossarySubMenu()`
- L530 — `void AdjustSlider(int delta)` — clamps 0..100, plays cue-volume preview via s_glossaryPreview or a SpeakUrgent sample for UrgentVolume
- L563 — `bool HandleInputRoot(int keyCode)` — Left/Right adjusts Slider rows, Up/Down steps focus (clamp not wrap), Enter dispatches by RowKind (Submenu pivots, Slider replays at delta=0, SkipIntros does a filesystem rename, else flips + persists a toggle), Esc calls Close()
- L648 — `bool HandleInputGlossary(int keyCode)` — Up/Down/Enter (arms delayed cue)/Esc (CloseGlossarySubMenu)
- L691 — `bool acc::menus::modsettings::HandleInput(int keyCode)` — auto-closes on foreground divergence, else routes to keybinds/glossary/root; unhandled GUI keys are still swallowed (Left/Right/Home/End/Activate) so the parent panel can't react underneath the open submenu
- L745 — `void acc::menus::modsettings::Tick()` — drives keybinds::Tick() and the glossary's delayed-fire cue (wrap-safe signed-subtract deadline check), rides priority group 0xb for pause-survival
- L780 — `bool acc::menus::modsettings::GetToggle(Option option)`
