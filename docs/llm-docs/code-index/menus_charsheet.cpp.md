# menus_charsheet.cpp (398 lines)

Implements the in-game character-sheet (CSWGuiInGameCharacter) opener
announce and its virtual stat-row chain entries. A spec table
(`k_statRowSpecs`) maps each stat (class, level, XP, HP, FP, six attributes,
alignment slider) to its label/slider struct offset, a format string, and a
synthetic sort-cy that forces reading order (stats above the real buttons)
independent of the engine's actual on-screen layout. The FP row is dropped
entirely for non-Force characters, gated on the engine's own "shown" bit
(bit 0x02 of lbl_force_stat's bit_flags, set/cleared by the engine's own
`SetStats`/`IsJedi` check) rather than re-derived — documented as far more
robust than re-deriving Force-user status locally. Several offset-naming
corrections are documented inline (verified against live sessions where an
earlier commit had reversed HP/FP or current/threshold XP).

## Declarations (in source order)

- L22 — `namespace acc::menus::charsheet`
- L56-81 — `constexpr size_t kCharSheetLbl*` offsets (Class, Level, Fort, Ref, Will, XpCur, XpThresh, DefStat, Fp, Hp, Str/Dex/Con/Int/Wis/Cha + their Mod pairs) + `kCharSheetSldAlign` (anonymous ns)
  note: several corrected vs. earlier commits — see file header comment for the verified sessions
- L92 — `constexpr uint32_t kControlShownBit = 0x2`
- L98 — `void ReadCharSheetLabel(void* panel, size_t offset, char* outBuf, size_t bufSize)` (anonymous ns)
- L121 — `bool DisplayedHasForce(void* panel)` (anonymous ns)
  note: reads the engine's own Force-user decision (lbl_force_stat's shown bit) rather than re-deriving
- L151 — `enum class StatRowKind { LabelValue, LabelValueMod, LabelValueThresh, Slider }` (anonymous ns)
- L158 — `struct StatRowSpec { valueOffset; modOffset; formatId; sortCy; kind; }` (anonymous ns)
- L166 — `constexpr StatRowSpec k_statRowSpecs[]` (anonymous ns)
  note: sortCy 1..12 anchors the stat block above real buttons (cy>=237) regardless of actual label coordinates
- L196 — `const StatRowSpec* FindSpecForControl(void* panel, void* labelControl)` (anonymous ns)
  note: FP row returns nullptr for non-Force characters — drops it from the chain entirely
- L217 — `bool IsStatRowAnchor(void* panel, void* labelControl)`
- L221 — `void ForEachStatRowAnchor(void* panel, callback, userData)`
- L240 — `bool ExtractStatRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)`
- L298 — `void MaybeAnnounce(void* panel)`
  note: first-open composed summary line; no per-panel "already spoken" guard (caller's IsSubScreenTracked already gates)
