# spectator_scene.cpp (89 lines)

Implements the Endar Spire spectator-battle cue. `kSceneTagPrefix` is
`"end_cut2_"` — intentionally widened past just the soldiers to also cover the
Sith cutting them down (`end_cut2_sith1..5`), because narrowing to soldiers
only left the Sith reading as normal, reachable enemies when cycled onto. A
per-soldier latch (`g_announced[]`, cap 32 for a 9-creature scene) speaks the
dramatic line once per distinct creature per area visit, not once per area —
an earlier once-per-area latch left all but the first soldier reading as
ordinary enemies, causing players to keep trying to engage them. Reset on area
pointer change. Speaks via `prism::Speak(..., interrupt=false)` so it follows
rather than cuts the name/brief the narration funnel just spoke.

## Declarations (in source order)

- L15 — `bool HasPrefixCI(const char* s, const char* prefix)`
- L33 — `constexpr char kSceneTagPrefix[] = "end_cut2_"`
- L43-46 — `constexpr int kMaxAnnounced = 32; void* g_lastArea; void* g_announced[32]; int g_announcedCount`
- L50 — `bool IsScriptedBattleSoldier(void* obj)`
- L59 — `const char* DramaticLine()`
- L63 — `void OnObjectNarrated(void* obj)`
  note: resets announced-set on area-pointer change; falls through to speak anyway if the (never-expected) 32-slot cap fills, per never-silence-the-fallback
