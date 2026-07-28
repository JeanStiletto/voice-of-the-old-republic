# state_overrides.cpp (172 lines)

Implements the state-label registry via `kOverrides[]`, a tag-keyed table of
`StateOverride{tag, offset, labels[], bitMask}`. Two mechanisms: `bitMask==0`
reads the dword at `offset` verbatim (Taris "Lights Out" wall switches,
CSWSPlaceable+0x260, empirically diffed 2026-05-26); `bitMask!=0` treats the
dword as the NWScript local-boolean word at CSWSObject+0x110 and tests one bit
(Star Forge captive-Jedi placeables and battle-droid terminals, bit 2 = 0x4,
confirmed by a log correlating the bit flip with an animation change and the
action queue emptying). `AppendStateLabel` reads the object's tag via
`engine::ReadCExoString` under SEH, looks up the override, reads the state
dword under SEH, and appends the matched label. Talks to strings.h for the
localised label text.

## Declarations (in source order)

- L17 — `struct LabelEntry { int value; strings::Id id; }`
  note: sentinel-terminated by id == Id::Count_
- L22 — `struct StateOverride { const char* tag; size_t offset; const LabelEntry* labels; uint32_t bitMask; }`
- L46 — `constexpr LabelEntry kWallSwitchLabels[]`
  note: 0=off(red/target state), 1=on — conservative initial guess, swappable if speech proves inverted
- L66-67 — `constexpr uint32_t kLocalBoolUsedMask = 1u<<2; constexpr size_t kLocalBoolWordOffset = 0x110`
  note: bit confirmed via patch-20260717-125936.log; an earlier guess (bit 19) had misread the script's globals-table constant
- L69 — `constexpr LabelEntry kCaptiveJediLabels[]`
- L75 — `constexpr LabelEntry kSfTerminalLabels[]`
- L81 — `constexpr StateOverride kOverrides[]`
  note: wall1-5 (Taris), sta_plc_captive2-8 (Star Forge Jedi), k45_plc_* + sta45_turretcomp (SF terminals)
- L105 — `const StateOverride* FindOverride(const char* tag)`
- L113 — `const char* PickLabel(const LabelEntry* labels, int value)`
- L123 — `bool AppendStateLabel(void* gameObject, char* outBuf, size_t bufSize)`
  note: both struct reads wrapped in __try/__except; bitMask path also logs raw bools + usable flag (+0x328) + anim (+0xd4) for polarity diagnosis
