# menus_extract.h (66 lines)

Header for the control-text extraction ladder. Documents the cycle-category cache contract (populated by OnSetActiveControl in menus.cpp before any activation runs; read internally by FromControl) and that `ownerPanel` lets callers who already know the panel skip the FindOwningPanel/g_currentPanel fallback resolution.

## Declarations (in source order)

- L40 — `const char* acc::menus::extract::FromControl(void* control, char* outBuf, size_t bufSize, void* ownerPanel = nullptr)` — returns a diagnostic source tag or nullptr
- L54 — `void acc::menus::extract::ResetCycleCategoryCache()` / L55 `void CaptureCycleCategory(void* control, const char* category)`
- L61 — `void acc::menus::extract::ForEachWagerRowAnchor(void* panel, bool(*callback)(void*,int,void*), void* userData)` — Pazaak wager virtual row registration, mirrors menus_credits
