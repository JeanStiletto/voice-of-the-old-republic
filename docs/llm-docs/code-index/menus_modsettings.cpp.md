# menus_modsettings.cpp (534 lines)

Mod-settings virtual overlay implementation. Manages a two-level menu (root options list + audio-glossary sub-list) grafted onto the engine's Options panels without any engine GUI.

## Declarations (in source order)

- L29 — `char s_rootSentinel = 0` — the sentinel byte whose address serves as the virtual chain-entry pointer
- L41 — `bool s_toggles[]` — toggle state array indexed by option index
- L64 — `bool s_open`, `int s_focused`, `void* s_parentPanel`, `void* s_fgAtOpen`, `bool s_glossaryOpen`, `int s_glossaryFocused`
- L78 — `bool s_pendingValid`, `DWORD s_pendingFireAt`, `NavCue s_pendingCue` — delayed audio-glossary cue state
- L79 — `constexpr DWORD kGlossaryDelayMs = 750`
- L83 — `enum class RowKind` — Toggle, Submenu (anonymous ns)
- L88 — `struct OptionSpec` — fields: option enum, kind, label string ID (anonymous ns)
- L94 — `constexpr OptionSpec k_options[]` — 4 entries: SpatialAudio toggle, AnnounceCombatLog toggle, AnnounceFootsteps toggle, AudioGlossary submenu (anonymous ns)
- L111 — `struct GlossaryEntry` — fields: label string ID, NavCue enum (anonymous ns)
- L116 — `constexpr GlossaryEntry k_glossary[13]` — audio cue name-to-cue mappings for the glossary submenu (anonymous ns)
- L137 — `const char* StateText(int optionIdx)` — returns "ein" / "aus" for toggles, "→" for submenus (anonymous ns)
- L151 — `void SpeakFocusedOption()` — formats and speaks the currently-focused root-level option (anonymous ns)
- L172 — `void SpeakFocusedGlossaryEntry()` — speaks the currently-focused glossary entry name (anonymous ns)
- L184 — `void CancelPendingGlossaryCue()` (anonymous ns)
- L205 — `bool ForegroundDivergedFromParent()` — compares current foreground against s_fgAtOpen baseline (anonymous ns)
- L218 — `void* acc::menus::modsettings::GetRootAnchor()`
- L222 — `bool acc::menus::modsettings::IsRootAnchor(void* control)`
- L226 — `void acc::menus::modsettings::ForEachRootAnchor(void* panel, bool (*callback)(...), void* userData)`
- L251 — `bool acc::menus::modsettings::ExtractRootLabel(char* outBuf, size_t bufSize)`
- L265 — `bool acc::menus::modsettings::IsOpen()`
- L269 — `void acc::menus::modsettings::OpenSubMenu(void* parentPanel)`
- L300 — `void acc::menus::modsettings::Close()`
- L331 — `void AutoCloseSilent()` — silently closes on foreground divergence without speaking the "closed" cue (anonymous ns — called from HandleInput)
- L345 — `namespace` (anonymous ns for glossary sub-menu helpers)
- L350 — `void OpenGlossarySubMenu()` (anonymous ns)
- L367 — `void CloseGlossarySubMenu()` (anonymous ns)
- L381 — `bool HandleInputRoot(int keyCode)` — Up/Down step, Enter toggle/pivot, Esc close (anonymous ns)
- L421 — `bool HandleInputGlossary(int keyCode)` — Up/Down step, Enter arm delayed cue, Esc return to root (anonymous ns)
- L464 — `bool acc::menus::modsettings::HandleInput(int keyCode)` — routes to glossary or root handler; auto-close on foreground divergence; blocks Left/Right/Home/End/Activate from parent
- L504 — `void acc::menus::modsettings::Tick()`
- L524 — `bool acc::menus::modsettings::GetToggle(Option option)`
