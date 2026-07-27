# transitions.cpp (1413 lines)

Implements area/room transition narration. Trigger moved off .lyt-room
ids (2026-05-22) onto wall_topology cluster ids, since K1 rooms flip on
94-98% of steps in dense areas; cluster ids are spatially stable. Layers:
flat landmark cache (proximity-scanned, per-area fog-of-war-scaled enter/
exit ranges, auto-discovery of script-gated map notes), cluster-change
dedup (text-equality + "flap" return-leg suppression + minor-cluster
dwell gate for small outdoor slivers), a Platz (multi-node cluster)
deferred-announce so the label resolves nearer the player's final
position, and a post-combat/UI-gate refire so a cluster change during
combat isn't lost. Talks to: wall_topology (LookupAt/BuildForArea/single
source of truth for perceptual labels), discovery, engine_area,
engine_panels, combat (IsCombatActive gate), narrated_target, prism.

## Declarations (in source order)

- L53-54 — `g_prev_area`, `g_prev_cluster_id` — last observed area/cluster
- L60 — `g_module_load_pending` — set in AnnouncePreLoadDestination, cleared on next fresh area
- L76-78 — cluster stability debounce (kClusterStabilityTicks=5)
- L101-104 — minor-cluster dwell gate constants (extent<12m, dwell 1800ms)
  note: added 2026-07-16 — outdoor slivers fired 100+ times vs 3 for the map's real region
- L130-138 — `struct Landmark` + `g_landmarks[128]` flat cache (name/pos/doorMatched/handle)
- L158 — `void SpeakArea(void* area)` — area-name announce, interrupt=false
- L189 — `void RebuildLandmarkCache(void* area)` — scans landmark waypoints w/ map_note_enabled
- L295-314 — `constexpr kFlapWindowMs/kFlapRadiusM` + `bool IsFlapRepeat(...)`
  note: suppresses only the RETURN leg of a boundary ping-pong, never a genuinely new label
- L318 — `void CommitSpokenLabel(text, pos, now)` — rotates 2-deep spoken-label history
- L367-372 — per-area landmark enter/exit range derived from fog-of-war cell size
- L383 — `void RecomputeLandmarkRanges()`
- L423 — `kMapNoteAutoDiscoverRangeM = 5.0f` — proximity auto-enable for script-gated notes
- L455 — `bool ResolveRoomSpeech(area, worldPos, outBuf, outSource, outClusterIdOpt)`
  note: two-tier — friendly room name (IsResrefStyleRoomName filtered) then wall_topology shape, gated by the RoomShapes mod-setting toggle
- L521 — `void LogWallTopoComparison(...)` — diagnostic cross-check log
- L558 — `void SpeakRoomChange(area, clusterId, worldPos)` — resolves + dedups (text/flap) + Platz-defers or speaks via SpeakUrgent
- L656 — `void TickPendingPlatz(area, playerPos)` — fires/supersedes the deferred Platz label after kPlatzDelayMs
- L716 — `void TickGatedClusterRefire(area, playerPos)` — re-speaks current room once the combat/UI gate clears
- L745 — `bool TryAutoDiscoverMapNote(waypoint, playerPos)` — enables a note the leader is standing on
- L775 — `void TickLandmarkCacheRecheck(area, playerPos)` — 1s-cadence drift check + rebuild + door-attach
- L819 — `void TickProximityLandmarks(playerPos)` — nearest-landmark stability + enter/exit hysteresis; skips door-matched entries
- L913 — `bool IsWorldSpeechGatedImpl()` — combat OR foreground-UI-blocking
- L922 — `bool IsResrefStyleRoomName(const char* name)`
- L938 — `bool FindLandmarkNear(pos, rangeM, nameOut, nameBufSize, posOut, outLandmarkIdx)`
- L968 — `bool IterateLandmarks(cursor, nameOut, nameBufSize, posOut, outLandmarkIdx)`
- L985 — `void MarkLandmarkClaimedByDoor(landmarkIdx)`
- L990 — `void Tick()` — movie-foreground guard, player/area resolve + reset-on-loss,
  area-change branch (rebuilds landmark cache + wall_topology graph, clears narrated_target),
  proximity landmarks, pending-Platz, gated refire, cluster-change trigger with stability +
  minor-dwell + open-area seam-holding
  note: kClusterIdOpenArea collapses to the previous committed cluster id when one exists, to suppress boundary-seam thrash (Kashyyyk Great Walkway)
- L1307 — `bool IsWorldSpeechGated()`
- L1311 — `bool IsModuleLoadPending()`
- L1315 — `void NotifyExternalLoadStarting(const char* reason)`
- L1326 — `void AnnouncePreLoadDestination(exoStringPtr)` — 2s dedup window, movie-foreground speech suppression
- L1398 — `extern "C" void OnSetMoveToModuleString(serverApp, arg_addr)` — entry hook @0x004aecd0
  note: LEA-vs-MOV KPatchManager wrapper bug — arg_addr is the STACK SLOT ADDRESS, must deref once for the real CExoString*
