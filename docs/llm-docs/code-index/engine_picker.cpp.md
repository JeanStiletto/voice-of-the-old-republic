# engine_picker.cpp (571 lines)

Implementation of engine_picker.h. SetMainInterfaceTarget's typedef carries
a required-but-unused second dword param (BYTES_PURGED=8) — a bare
single-arg typedef under-pushes by 4 and corrupts the caller's frame (fixed
2026-06-18, patch-20260618-162656.log DIAG[1] vs DIAG[2]).

## Declarations (in source order)

- L27 — `namespace { ... }` (anonymous, TU-local offsets + helpers)
- L48 — CClientExoAppInternal field offsets: `kInternalGuiInGameOffset`=0x040, `kInternalLastTargetOffset`=0x2b4, `kInternalLastClickedOnTargetOffset`=0x2b8, `kInternalHoverTargetOffset`=0x4a4, `kInternalDescriptorArrayOffset`=0x4c8, `kInternalDescriptorCountOffset`=0x4cc
- L58 — `constexpr size_t kGuiInGameMainInterfaceOffset = 0x90`
- L61 — CSWGuiInterfaceAction offsets: label/id/fn/target/icon, `kInterfaceActionStride`=0x38
- L73 — Engine entry point addresses: GetDefaultActions @0x00620620, HandleMouseClickInWorld @0x00620350, SetMainInterfaceTarget @0x0062b000, PopulateMenus @0x00689d80
- L83 — `typedef void (__thiscall* PFN_GetDefaultActions)(void*)`
- L84 — `typedef void (__thiscall* PFN_HandleMouseClickInWorld)(void*)`
- L93 — `typedef void (__thiscall* PFN_SetMainInterfaceTarget)(void*, uint32_t, uint32_t)`
  note: two-dword typedef required — SetMainInterfaceTarget purges 8 bytes (ret 8) though only param_1 is referenced.
- L96 — `typedef void (__thiscall* PFN_PopulateMenus)(void*)`
- L98 — `void* GetClientExoApp()`
- L110 — `void* GetClientExoAppInternal(void* exoApp)`
- L121 — `void* GetGuiInGame(void* internal)`
- L132 — `void* GetMainInterface(void* guiInGame)`
- L143 — `void WriteUInt32(void* base, size_t offset, uint32_t value)`
- L155 — `uint32_t ReadUInt32(void* base, size_t offset)`
- L168 — `bool CopyCStringSafe(const char* src, char* dst, size_t cap)`
- L190 — `void ReadResRef(void* base, size_t offset, char* out, size_t outCap)`
  note: treats the 16-byte CResRef as NUL-terminated within the window.
- L213 — `void ReadExoString(void* base, size_t offset, char* out, size_t outCap)`
- L229 — `void SnapshotDescriptor(void* internal, ActionSnapshot* snap)`
  note: valid only when descriptor pointer non-null AND count > 0.
- L252 — `namespace acc::picker`
- L254 — `bool Drive(uint32_t targetServerHandle, ActionSnapshot* outSnapshot, bool forceRadial, bool populateOnly)`
  note: 5-step flow — SetMainInterfaceTarget, GetDefaultActions, snapshot, (empty/forceRadial: PopulateMenus radial fallback + wide diag), else click-gate write + conditional input-disable (skipped for IsWalkToActVerb) + HandleMouseClickInWorld dispatch.
- L482 — `bool ReanchorRadial(uint32_t targetServerHandle)`
- L522 — `bool ReadCurrent(ActionSnapshot* outSnapshot)`
- L535 — `const uintptr_t kAddrCSWCCreatureActionInitiateDialog = R(0x0060f620)`
- L536 — `typedef void (__thiscall* PFN_ActionInitiateDialog)(void*, uint32_t, void*)`
- L540 — `bool InitiateDialog(uint32_t targetServerHandle)`
