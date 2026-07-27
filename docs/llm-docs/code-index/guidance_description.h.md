# guidance_description.h (31 lines)

Header for the turn-by-turn route readout. Documents the output shape
("Route to door: 5 metres north, 4 metres north-east, ..., no transition")
and the same-sector merge / sub-1m fold rules implemented in the .cpp.

## Declarations (in source order)

- L21 — `namespace acc::guidance::description`
- L25 — `bool Speak(const Vector& playerPos, const std::vector<Vector>& waypoints, const char* targetName, bool isTransition, bool interrupt)`
  note: empty waypoint list speaks the localised "no path" phrase.
