# menus_extract.h (57 lines)

Public surface for the control-text extraction module. Declares `namespace acc::menus::extract`.

## Declarations (in source order)

- L1 — `namespace acc::menus::extract`
- L10 — `const char* FromControl(void* control, char* outBuf, size_t bufSize, void* ownerPanel = nullptr)` — the announce ladder; returns a source tag string on success, nullptr if no text found
- L20 — `void ResetCycleCategoryCache()`
- L22 — `void CaptureCycleCategory(void* control, const char* category)` — stores the pre-activation category text for a cycle widget so FromControl can prefix it after the engine overwrites the value button
