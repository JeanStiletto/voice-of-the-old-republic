# radial_menu.cpp (357 lines)

Implementation of the radial menu gate. Up/Down skips empty rows;
Left/Right cycles actions within a row via engine SelectNext/Prev;
Enter dispatches via DoTargetAction; Esc drops the active flag only.
Tick auto-disarms when all rows drain and repairs curRow when only the
current row drains. ScheduleWideDiag runs independently of active flag.

## Declarations (in source order)

- L15 — `namespace acc::radial_menu`
- L17 — `namespace` (anonymous)
- L20 — `struct State`
  note: holds active flag, curRow, and target name copy for re-announce.
- L30 — `struct DiagSchedule`
  note: tracks frames-remaining + ordinal for the multi-frame wide TAM diagnostic dump.
- L44 — `int FindPopulatedRow(void* tam, int start, int dir)`
  note: wrapping search for next/prev row with RowActionCount > 0; start is exclusive; returns -1 when no row has actions.
- L57 — `int FirstPopulatedRow(void* tam)`
  note: linear scan from row 0; returns -1 if none — caller declines to arm.
- L69 — `void SpeakRowAction(void* tam, int row, const char* prefix)`
  note: speaks "Aktion {ordinal}/{total}: {label}"; prefix prepended unchanged (used on initial open with target name).
- L99 — `void SpeakCurrentLabel(void* tam, int row)`
  note: speaks current row label only — used after Left/Right cycling where the row counter would be redundant.
- L112 — `bool ArmAfterPopulate(const char* targetName)`
- L163 — `bool IsActive()`
- L167 — `bool HandleInputEvent(int code, int value)`
- L287 — `void ScheduleWideDiag(int frames, const char* tag)`
- L302 — `void Tick()`
- L349 — `void ForceDisarm(const char* reason)`
