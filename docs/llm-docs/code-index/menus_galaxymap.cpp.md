# menus_galaxymap.cpp (200 lines)

Accessibility layer for the galaxy/star-map travel screen (CSWGuiInGameGalaxyMap). Rather than exposing the image-only planet-hotspot buttons through the generic chain, drives the engine's own `HandleInputEvent` directly at address 0x00695980 so its native NextPlanet/PrevPlanet handlers (which already skip unavailable/unselectable planets per CSWPartyTable) do the filtering for us. Up/Down cycle planets and re-announce LBL_PLANETNAME; Enter travels; Esc cancels; Shift+Down peeks LBL_DESC. First-sight (`Tick`) speaks a fixed title + current planet once per panel instance via a single-slot latch (`s_announcedPanel`). Talks to `engine_manager` (panels[] walk), `menus_pending` (QueueGalaxyInput defers the dispatch), `prism`.

## Declarations (in source order)

- L24 — `constexpr size_t kPlanetNameLabelOffset = 0x1ca4`, `kDescriptionLabelOffset = 0x1de4` — CSWGuiInGameGalaxyMap field offsets, SIZE 0x2550
- L35 — `constexpr int kEngineAccept/Cancel/Prev/Next` — engine HandleInputEvent codes (0x27/0x28/0x2f/0x30)
- L42 — `bool ReadLabel(panel, labelOffset, outBuf, bufSize)` (anonymous ns) — SEH-wrapped gui_string read
- L55 — `void AnnouncePlanetName(panel, interrupt)`
- L66 — `void* s_announcedPanel` — first-sight latch (singleton panel)
- L68 — `void* FindGalaxyMapInPanels()` — walks manager panels[] for PanelKind::InGameGalaxyMap
- L85 — `bool acc::menus::galaxymap::IsGalaxyMapPanel(void* panel)`
- L89 — `bool acc::menus::galaxymap::TryHandleInput(activePanel, param_1, param_2, rv)` — owns Up/Down/Enter/Esc + other nav keys (no-op re-announce); consumes on both edges so chain nav never walks unnamed planet buttons
- L142 — `bool acc::menus::galaxymap::SpeakDescription(void* panel)` — Shift+Down peek of LBL_DESC
- L154 — `void acc::menus::galaxymap::DispatchInput(panel, engineCode, announcePlanet)` — deferred engine call via the pending-op drain; calls HandleInputEvent by fixed address, SEH-guarded
- L178 — `void acc::menus::galaxymap::Tick()` — first-sight announce (title + current planet), latch clears when the map closes
