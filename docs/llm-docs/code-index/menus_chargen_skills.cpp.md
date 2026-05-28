# menus_chargen_skills.cpp (354 lines)

Chargen Skills panel accessibility implementation. Handles chain-step suffix (rank/cost/class-skill marker) and value-change announcement.

## Declarations (in source order)

- L21 — `bool acc::menus::chargen_skills::IsChargenSkillsPanel(void* panel)`
- L32 — `int acc::menus::chargen_skills::SkillIndexFromButton(void* panel, void* control)`
- L45 — `void acc::menus::chargen_skills::SyncSelectedSkillFromChainFocus()`
- L76 — `void acc::menus::chargen_skills::CaptureLabelsIfApplicable(void* panel)`
- L140 — `bool ReadButtonTextDirect(void* button, char* outBuf, size_t bufSize)` (anonymous ns)
- L164 — `bool ReadLabelTextAt(void* panel, size_t offset, char* outBuf, size_t bufSize)` (anonymous ns)
- L195 — `typedef int (__thiscall* PFN_IsClassSkill)(void* this_, unsigned short skillIdx)` (anonymous ns)
- L197 — `int ReadEngineSkillCost(void* panel, int skillIdx)` — calls engine's thiscall to get the point cost for the skill (1 for class skills, 2 for cross-class) (anonymous ns)
- L213 — `void acc::menus::chargen_skills::AnnounceChainStepSuffix(void* panel, void* control)`
- L238 — `bool acc::menus::chargen_skills::AnnounceChainStepDescription(void* panel, void* control)`
- L317 — `bool acc::menus::chargen_skills::IsChargenSkillsDescriptionListbox(void* listBox)`
- L326 — `bool acc::menus::chargen_skills::AnnounceValueChange(void* panel, void* control)`
