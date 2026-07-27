# guidance_description.cpp (194 lines)

Turn-by-turn TTS readout of a computed waypoint path: builds merged compass-
sector segments (SegmentBuilder folds consecutive same-sector hops so a long
corridor reads as one "20 metres north" instead of five short ones; sub-1m
hops accumulate into a pending bucket rather than being classified) and
speaks "Route to <target>: total <N>m: <seg1>, <seg2>, ... <transition
note>." Handles empty-path ("no path") and already-there (all-sub-threshold)
cases. Talks to engine_compass (EngineYawToCompass/CompassToSector) and
prism.

## Declarations (in source order)

- L12 — `namespace acc::guidance::description`
- L16 — `constexpr float kMinSegmentMeters = 1.0f`
- L20 — `struct Segment { int sector; float dist; }`
- L29 — `int CompassSectorOf(float dx, float dy)`
- L42 — `class SegmentBuilder` — `AddHop`, `Segments()`, `Pending()`
  note: sub-threshold hops accumulate in pending_ and attach to whichever segment (same-sector merge or new) follows.
- L85 — `int FormatSegment(const Segment&, char* outBuf, size_t)`
- L97 — `int TotalMetres(const std::vector<Segment>&, float pending)`
- L107 — `bool Speak(const Vector& playerPos, const std::vector<Vector>& waypoints, const char* targetName, bool isTransition, bool interrupt)` — public
