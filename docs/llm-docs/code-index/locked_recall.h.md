# locked_recall.h (53 lines)

Header for the locked-object story-bark recall module. Explains the problem:
the engine's flavour bark for WHY a door/container is locked plays once and
is then game-side guarded, so a blind player who misses it never hears it
again — only the unhelpful generic "This object is locked" repeats. This
module captures and replays that bark per-object.

## Declarations (in source order)

- L32 — `void RegisterMsgRule()` — call once alongside other Register*MsgRules() at router bootstrap
- L40 — `bool IsGenericLockedMessage(const char* text)`
  note: shared so other router rules can hang area-specific door guidance off the engine's own locked report rather than intercepting interact dispatch (see interact_hotkey.cpp's WARNING on why that distinction matters); false until the TLK is live
- L46 — `void MaybeCapture(const char* barkText, bool ownerless)` — no-op unless within the capture window and no cached explanation exists yet
- L50 — `void Tick()` — flushes a queued replay after the router's own "locked" line
