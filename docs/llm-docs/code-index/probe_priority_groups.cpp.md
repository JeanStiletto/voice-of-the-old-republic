# probe_priority_groups.cpp (166 lines)

Implementation of the priority-groups probe. Walks
CExoSoundPtr→CExoSoundInternal+0x4c→CPriorityGroup[] (0x18-byte stride, up
to 64 entries). Key layout correction documented: CExoSound facade is
4 bytes wide (no vtable, internal at +0x0), unlike CClientExoApp's 8-byte
layout — reading from +0x4 walks off the end and faults.

## Declarations (in source order)

- L10 — `namespace acc::probe::priority_groups`
- L54 — `void* GetSoundInternal()`
  note: SEH-guarded; reads CExoSoundPtr → CExoSound+0x0 (internal); offset is 0x0 not 0x4
- L66 — `void DumpOnce()`
  note: inner implementation; uses garbage-run heuristic (4 consecutive invalid rows) to detect array end; sets g_dumped=true on completion
- L152 — `void Tick()`
