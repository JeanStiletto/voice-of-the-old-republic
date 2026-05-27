# engine_actionbar.cpp (306 lines)

Implementation of engine_actionbar.h. No leading comment block.

## Declarations (in source order)

- L12 — `namespace { ... }` (anonymous, file-scope helpers)
- L66 — `void* GetClientExoApp()`
  note: local copy of the same chain walk in engine_picker/engine_radial
- L78 — `void* GetClientExoAppInternal(void* exoApp)`
- L89 — `void* GetGuiInGame(void* internal)`
- L100 — `void* GetMainInterface(void* guiInGame)`
- L111 — `bool ReadInt32(void* base, size_t offset, int32_t* out)`
- L122 — `void* ReadPtr(void* base, size_t offset)`
- L134 — `void* DescriptorAddr(void* mi, int slot, int index)`
  note: returns address of CSWGuiInterfaceAction at [slot][index] in the personal-lists array
- L151 — `bool ReadCExoStringLocal(void* base, size_t offset, char* outBuf, size_t bufSize)`
- L175 — `namespace acc::engine_actionbar`
- L177 — `void* ResolveMainInterface()`
- L184 — `int VariantCount(void* mi, int slot)`
- L194 — `bool ReadVariantLabel(void* mi, int slot, int index, char* outBuf, size_t bufSize)`
- L203 — `uint32_t ReadVariantActionId(void* mi, int slot, int index)`
- L211 — `void* GetColumnActionButton(void* mi, int slot)`
- L222 — `bool SelectVariant(void* mi, int slot, int index)`
- L238 — `bool FireSelectedVariant(void* mi, int slot)`
- L254 — `void LogState(void* mi, const char* tag)`
- L271 — `bool PrepareBareDispatch(uint32_t targetClientHandle)`
