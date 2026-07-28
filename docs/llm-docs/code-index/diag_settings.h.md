# diag_settings.h (15 lines)

Public surface for the one-shot swkotor.ini + install-root diagnostic snapshot.

## Declarations (in source order)

- L12 — `void LogStartupSnapshot()`
  note: idempotent; logs every ini section/key, dsound/dsoal/dinput8/mss32 DLL presence, and Override\ file count
