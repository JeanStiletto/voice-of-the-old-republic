# locked_recall.cpp (162 lines)

Caches each locked door/container's one-time story-explanation bark (an
ownerless BarkBubble fired alongside the engine's generic "This object is
locked" feedback, strref 1437) and replays it on repeat locked-interaction
attempts, since the engine only speaks the flavour bark once per session.
Registers a non-consuming rule with `msg_router` that keys off the
narrated-target handle at the moment of a locked message; `MaybeCapture` (fed
by the BarkBubble handler) stores the next ownerless bark within a 2.5s
window as that object's explanation; `Tick` flushes a queued replay. Ring
buffer of 32 entries, oldest overwritten. Talks to `narrated_target`,
`msg_router`, `engine_reads::LookupTlk` (language-independent strref match).

## Declarations (in source order)

- L21 — `constexpr uint32_t kLockedFeedbackStrref = 1437`
- L27 — `constexpr unsigned int kCaptureWindowMs = 2500`
- L29 — `char s_lockedText[256]` / `bool s_lockedTextBuilt` — lazily resolved via LookupTlk, retried until the TLK table is live
- L35 — `struct Entry { uint32_t handle; char bark[512]; }` — L39 `kMaxEntries = 32`, ring-overwrite cache
- L44 — `uint32_t s_recentHandle` / `s_recentTickMs` — most-recently-locked object + timestamp
- L47 — `char s_pendingReplay[512]` — queued replay text for Tick()
- L52 — `bool EqualsTrimmed(const char* a, const char* b)` — trim-insensitive compare
- L63 — `bool IsLockedMessage(const char* text)` — lazy TLK-resolve + trimmed compare
- L76 — `const char* Lookup(uint32_t handle)` / L83 `void Store(uint32_t handle, const char* bark)` — ring-buffer cache ops
- L106 — `bool RuleLocked(const char* text)`
  note: never consumes; on a repeat lock queues the cached bark, on a first lock just notes the handle+timestamp awaiting a bark
- L135 — `void RegisterMsgRule()` — adds "LockedRecall" rule to msg::Router
- L139 — `bool IsGenericLockedMessage(const char* text)` — exposed so other rules (door guidance) can hang off strref 1437 instead of intercepting interact dispatch
- L143 — `void MaybeCapture(const char* barkText, bool ownerless)` — one-shot capture per locked interaction, consumes s_recentHandle
- L154 — `void Tick()` — speaks + clears s_pendingReplay
