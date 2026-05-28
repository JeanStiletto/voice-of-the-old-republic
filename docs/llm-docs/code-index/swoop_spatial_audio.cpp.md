# swoop_spatial_audio.cpp (~595 lines)

Per-tick obstacle and accelerator-pad spatial-audio sweeps for the swoop race. Walks the global CSWMiniGameObjectArray, downcasts each slot via vtable AsObstacle / AsEnemy, drives one LoopSource per in-range obstacle (multi-source) and one nearest-only LoopSource for accelpads. Split from swoop_race.cpp on 2026-05-27.

## Declarations (in source order)

### Anonymous namespace
- Engine offsets: kObstacleAurObjectOffset, kAurObjectPositionOffset, kClientInternalMgoArrayOffset, kMgoArrayObjectsOffset, kMgoArraySlotCount, kVtableSlotAsObstacle, kVtableSlotAsEnemy, kAurVtableSlotGetName, kAccelpadLoopResref, kAccelpadCueRangeM, kAccelpadConcurrentLoops, kTrackFollowerModelsDataOffset, kModelVtableSlotGetPosition, kObstacleCueRangeM, kObstacleForwardMargin, kObstacleWarnLoopResref, kObstacleDistanceCompression, kObstacleMinSourceDistanceM
- `struct SpatialAudioState` — owns the obstacle_loops[255] + accelpad_loops[255] arrays and the two diagnostic guards
- `SpatialAudioState g_state` — module-local singleton
- `void* SafeReadPtr(void*, size_t)` — SEH-guarded pointer read
- `bool SafeReadVector(void*, size_t, Vector&)` — SEH-guarded Vector read
- `void* ResolveMgoArray()` — AppManager → CClientExoApp → CClientExoAppInternal → CSWMiniGameObjectArray chain
- `typedef void* (__thiscall* PFN_AsCast)(void*)` — vtable downcast signature
- `typedef const char* (__thiscall* PFN_GetAurName)(void*)` — CAurObject GetName signature
- `const char* ReadAurObjectName(void*)` — CAurObject vtable[+0xc] thunk
- `typedef Vector* (__thiscall* PFN_GetPositionThunk)(void*, Vector*)` — CSWTrackFollower model-position signature
- `bool ReadTrackFollowerPosition(void*, Vector&)` — model[0]->vtable[+0x64] thunk
- `void* CallAsCast(void*, size_t)` — generic vtable downcast invoker
- `void TickObstacleCues(void*)` — 255-slot sweep, AsObstacle downcast, multi-source loop policy with 1:9 distance compression
- `void TickAccelpadCues(void*)` — sibling sweep, AsEnemy downcast, nearest-only loop policy

### Public entry points
- `void TickSpatialAudio(void* miniGame)` — calls TickObstacleCues + TickAccelpadCues
- `void ResetSpatialAudio()` — clears diagnostic flags, stops every loop
