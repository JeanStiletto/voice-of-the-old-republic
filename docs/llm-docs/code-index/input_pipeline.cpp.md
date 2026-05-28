# input_pipeline.cpp

Implementation of the engine-input pipeline. In addition to providing
`NextSeq()`, this TU contains two C-linkage hook handlers registered via
hooks.toml:

- `OnProcessInput` — frame-boundary seq tick (currently silent on the log;
  the per-frame "Diag.ProcInput" line was removed for volume, but the seq
  bump encodes frame boundaries as gaps in the other two streams).
- `OnClientHandleInputEvent` — logs every event the upstream client-app
  HandleInputEvent sees, with translated key names and caller EIP. Also
  performs the bare 1-7 dispatch prep (PrepareBareDispatch + variant
  restamp for personal-action and target-action slots) and the Q/E
  re-announce deferral via `RequestQEReannounce`. Implements Bug-2a fix:
  forwards arrow keys to the manager when a modal is on the stack.

## Declarations (in source order)

- `namespace acc::input`
- `unsigned int NextSeq()`
- `extern "C" void __cdecl OnProcessInput(void* /*this_ptr*/)`
  note: hook at CClientExoAppInternal::ProcessInput; only bumps the seq counter; log line removed for volume
- `extern "C" void __cdecl OnClientHandleInputEvent(void* this_ptr, void* p1_addr, void* p2_addr)`
  note: hook at CClientExoAppInternal::HandleInputEvent; does logging + bare 1-7 prep + Q/E re-announce deferral + Bug-2a modal arrow-key forwarding
