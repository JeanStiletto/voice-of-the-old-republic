# engine_options.h (37 lines)

CClientOptions helpers — currently the "Mouse Look" toggle. Documents the chain AppManager → CClientExoApp → Internal → CClientOptions and the bitfield layout (+0x8, 5 bits: auto_level/mouse_look/autosave/minigame_yaxis/combat_movement).

## Declarations (in source order)

- L19 — `namespace acc::engine`
- L22 — `bool GetMouseLook(bool& out)`
- L25 — `void* GetClientOptions()`
  note: exposed for diagnostic probes; production code should use Get/Set/ToggleMouseLook
- L27 — `bool SetMouseLook(bool enabled)`
- L30 — `bool ToggleMouseLook(bool& outNew)`
  note: read-modify-write; false on either failure; outNew = new value on success
- L35 — `constexpr unsigned int kClientAppOptionsOffset      = 0x4`
- L36 — `constexpr unsigned int kClientOptionsBitFieldOffset = 0x8`
- L37 — `constexpr unsigned int kClientOptionsMouseLookMask  = 0x2`
