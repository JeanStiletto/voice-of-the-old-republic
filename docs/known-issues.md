# Known Issues

Status tracker for accessibility-mod work, in five buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.
- **Beta Preparations** — non-feature work that must land before a public beta.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

### `engine_player::GetPartyMembers` reads party table from facade pointer

The function at `patches/Accessibility/engine_player.cpp:368` walks `AppManager → serverApp + kServerExoAppPartyTableOffset (0x1b770)`, where `serverApp` is the public 8-byte vtable+internal facade. The party table actually lives on the *internal* — `serverApp+4 → internal + 0x1b770`. The header at `engine_player.h:160-170` documents the bug pattern explicitly ("Earlier walks read from facade+0x1b770 — wrong; returned random heap (all 1s)") and even introduced `kServerExoAppPartyTableOffset` as a legacy alias of `kServerInternalPartyTableOffset` to flag remaining wrong-base callers. `GetPartyMembers` is the last surviving wrong-base caller.

Three consumers (`combat_queue`, `combat_special_watch`, `party_cache`) currently rely on this read. They've apparently been working acceptably in the field — either through compensating logic or because the wrong-memory results happen to be benign — so the bug is latent rather than user-visible.

Memory: `project_cserverexoapp_facade_split.md`.

Fix: redirect `GetPartyMembers` to `serverInternal + 0x1b770` (same chain `GetServerPartyTable` at `engine_player.cpp:398` already uses), then verify the three consumers still behave correctly. Test in any scene with companions (Taris apartment after recruiting Carth + Mission + Zaalbar is the canonical multi-party scene).

## Planned

## Monitor

## Polish

## Beta Preparations
