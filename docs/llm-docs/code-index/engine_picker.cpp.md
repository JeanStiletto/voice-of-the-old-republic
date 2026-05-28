# engine_picker.cpp (451 lines)

Implementation of engine_picker.h. No leading comment block.

## Declarations (in source order)

- L27 — `namespace { ... }` (anonymous, TU-local helpers)
- L83 — `typedef void (__thiscall* PFN_GetDefaultActions)(void*)`
- L84 — `typedef void (__thiscall* PFN_HandleMouseClickInWorld)(void*)`
- L85 — `typedef void (__thiscall* PFN_SetMainInterfaceTarget)(void*, uint32_t)`
- L87 — `typedef void (__thiscall* PFN_PopulateMenus)(void*)`
- L89 — `void* GetClientExoApp()`
- L101 — `void* GetClientExoAppInternal(void* exoApp)`
- L112 — `void* GetGuiInGame(void* internal)`
- L123 — `void* GetMainInterface(void* guiInGame)`
- L134 — `void WriteUInt32(void* base, size_t offset, uint32_t value)`
- L146 — `uint32_t ReadUInt32(void* base, size_t offset)`
- L157 — `bool CopyCStringSafe(const char* src, char* dst, size_t cap)`
- L181 — `void ReadResRef(void* base, size_t offset, char* out, size_t outCap)`
  note: 16-byte fixed-buffer CResRef treated as NUL-terminated within window
- L205 — `void ReadExoString(void* base, size_t offset, char* out, size_t outCap)`
- L219 — `void SnapshotDescriptor(void* internal, acc::picker::ActionSnapshot* snap)`
  note: reads +0x4c8 descriptor array pointer and +0x4cc count; populates snap only when count > 0
- L243 — `namespace acc::picker`
- L245 — `bool Drive(uint32_t targetServerHandle, ActionSnapshot* outSnapshot, bool forceRadial)`
- L442 — `bool ReadCurrent(ActionSnapshot* outSnapshot)`
