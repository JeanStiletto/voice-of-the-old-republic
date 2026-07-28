# probe_priority_groups.cpp (167 lines)

One-shot diagnostic that walks `CExoSoundInternal.priority_groups[N]` (the
table `PlayOneShotSound`/`Play3DOneShotSound` index via the `priority_group`
byte) and logs each entry's volume/priority/distance/variance/fade_time.
Purpose: `SetPriorityGroup`'s decompile shows the per-group volume scalar
multiplies a cue's byte volume, so switching priority group can implicitly
amplify a cue without touching its own volume — this probe surfaces the
table to find the loudest group. Walks up to 64 speculative entries and bails
after 4 consecutive rows that look like garbage (both volume and priority
bytes > 127). Fires once, ~2s after first tick, to let the sound subsystem
settle.

## Declarations (in source order)

- L19 — `constexpr size_t kCExoSoundInternalOffset = 0x0`
  note: CExoSound facade is only 4 bytes wide with no vtable — internal lives at offset 0, unlike CClientExoApp's 8-byte/vtable@0/internal@4 layout; an earlier probe attempt read +0x4 and faulted.
- L22 — `constexpr size_t kSoundInternalPriorityGroupsOff = 0x4c`
- L35-42 — CPriorityGroup layout (stride 0x18): `kPriorityGroupStride, kPriorityGroupMaxPlayerOff=0x04, kPriorityGroupPriorityOff=0x06, kPriorityGroupVolumeOff=0x07, kPriorityGroupMinDistOff=0x08, kPriorityGroupMaxDistOff=0x0C, kPriorityGroupVarianceOff=0x10, kPriorityGroupFadeTimeOff=0x14`
- L49 — `constexpr int kMaxEntries = 64`
- L51-52 — `bool g_dumped; DWORD g_firstObservedAt`
- L54 — `void* GetSoundInternal()`
- L66 — `void DumpOnce()`
  note: heuristic garbage-detection (vol<=127 && prio<=127) with a bail-after-4-consecutive-invalid-rows rule.
- L152 — `void Tick()`
  note: no-op after first successful dump; waits ~2s after first observation before reading, mirroring other probes' engine-init settle convention.
