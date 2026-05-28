# core_tick.cpp (162 lines)

Per-tick dispatcher implementation. Fan-out to all subsystems in load-bearing order (camera_announce → camera_orient → spatial::change_detector → transitions → view_mode). Also owns the extern "C" OnUpdate hook entry.

## Declarations (in source order)

- L41 — `namespace acc::tick`
- L43 — `void Dispatch()`
  note: explicit call order is architecturally significant; see block comment around L89-99 for ordering constraints
- L159 — `extern "C" void __cdecl OnUpdate(void* gmFromEbp)`
  note: hook on CSWGuiManager::Update @0x40ce76; forwards to acc::tick::Dispatch()
