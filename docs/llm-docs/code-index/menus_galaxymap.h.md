# menus_galaxymap.h (45 lines)

Header for the galaxy-map travel-screen accessibility layer. Documents that planet hotspots are image-only buttons with no readable caption and the selection state lives server-side on CSWPartyTable, hence driving the engine's own HandleInputEvent instead of the generic chain.

## Declarations (in source order)

- L23 — `bool acc::menus::galaxymap::IsGalaxyMapPanel(void* panel)`
- L30 — `bool acc::menus::galaxymap::TryHandleInput(void* activePanel, int param_1, int param_2, int& rv)`
- L34 — `bool acc::menus::galaxymap::SpeakDescription(void* panel)` — Shift+Down peek, caller consumes regardless of return
- L39 — `void acc::menus::galaxymap::DispatchInput(void* panel, int engineCode, bool announcePlanet)` — invoked from the pending-op Drain
- L43 — `void acc::menus::galaxymap::Tick()` — first-sight announce, called once per tick from TickMonitors
