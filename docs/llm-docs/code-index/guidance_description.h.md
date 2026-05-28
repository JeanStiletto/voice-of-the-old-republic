# guidance_description.h (31 lines)

Turn-by-turn TTS readout of a computed path. Same-sector segments merge; sub-1m hops fold into the next. Transition suffix: "1 transition" or "no transition".

## Declarations (in source order)

- L21 — `namespace acc::guidance::description`
- L25 — `bool Speak(const Vector& playerPos, const std::vector<Vector>& waypoints, const char* targetName, bool isTransition, bool interrupt);`
  note: empty waypoints speaks the localised "no path" phrase and returns false; targetName is caller-resolved
