# engine_app.cpp (46 lines)

Implementation of the three AppManager resolve primitives declared in
`engine_app.h` — read that entry for the chain, the rationale and the K2-port
note.

## Shape

Each function is a null-check plus one guarded dereference, layered:
`GetAppManager()` reads the global, `GetServerApp()` builds on it,
`GetServerAppInternal()` builds on that. Every one returns nullptr on a null
link or an SEH fault.

Each walk carries its own `__try` rather than sharing one across the chain: a
fault reading the `.data` global is a different situation from a fault reading
a facade field, and both have to degrade to nullptr for callers running during
engine teardown.

The layering costs one extra pointer read per hop versus a single fused walk.
That is deliberate — it is one dereference, and it buys callers the ability to
tell "no AppManager yet" from "AppManager without a server", which
`engine_subscreen.cpp`'s pause diagnostics rely on.
