# guidance_description.cpp (194 lines)

Implementation of the turn-by-turn path description. Builds merged segment list via SegmentBuilder, formats per-segment "{metres} Meter {direction}", assembles the route header, and speaks via Prism.

## Declarations (in source order)

- L12 — `namespace acc::guidance::description`
- L14 — `namespace { // anonymous`
- L16 — `constexpr float kMinSegmentMeters = 1.0f;`
  note: hops shorter than this are accumulated into a pending bucket and merged, not spoken independently
- L18 — `constexpr float kRadToDeg = 57.29577951308232f;`
- L20 — `struct Segment`
  note: one merged compass sector + distance in metres
- L29 — `int CompassSectorOf(float dx, float dy)`
  note: converts a displacement vector to an 8-point compass sector (0..7) via EngineYawToCompass + CompassToSector
- L42 — `class SegmentBuilder`
  note: stateful builder that folds consecutive same-sector hops and accumulates sub-threshold hops into a pending bucket
- L44 — `    void AddHop(float dx, float dy)`
- L74 — `    const std::vector<Segment>& Segments() const`
- L76 — `    float Pending() const`
- L85 — `int FormatSegment(const Segment& seg, char* outBuf, size_t outBufSize)`
- L97 — `int TotalMetres(const std::vector<Segment>& segs, float pending)`
- L105 — `} // namespace (anonymous)`
- L107 — `bool Speak(const Vector& playerPos, const std::vector<Vector>& waypoints, const char* targetName, bool isTransition, bool interrupt)`
