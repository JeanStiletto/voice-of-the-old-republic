# engine_offsets.h (28 lines)

Thin aggregator. Since the Phase-2 C8 split (2026-07-29) this file holds no
constants of its own — it includes the four headers the table was cut into and
exists so that all 86 pre-existing includers keep compiling unchanged.

Include this to get everything (nothing changed for existing callers); include
a narrower header when you only need that class of constant.

## The family

- **`engine_offsets_types.h`** — plain C++ struct layouts (`CExoArrayList`,
  `Vector`, `CExoString`). No addresses, no offsets. Also carries the
  split's rationale comment, so read it first.
- **`engine_offsets_addresses.h`** — 105 constants: `.text` function and vtable
  addresses (all through `acc::addr::R()`) plus the two raw `.data` global
  pointers, and the 15 `PFN_*` calling-convention typedefs.
- **`engine_offsets_fields.h`** — 244 constants: struct field offsets, plus the
  geometry (strides, counts, sizeof) and bit-mask/sentinel constants that
  decode the field they sit next to.
- **`engine_offsets_values.h`** — 18 constants: vtable slot indices, TLK
  strrefs, object-handle sentinels, action-type enum bytes, panel-internal
  input codes.

## Why this axis

The cut mirrors the upstream KPatchManager AddressDatabase taxonomy
(`functions` / `global_pointers` / `offsets`), so each group later maps 1:1
onto a `GameVersion::Get*` query rather than needing a redesign. The fourth
group — resource-derived values — has no address-database counterpart on
purpose: strrefs and `.gui` control IDs follow `dialog.tlk` and the layout
files, not the executable.

On a KOTOR 2 port the three groups move independently: every address is wrong,
struct offsets are mostly but not entirely wrong (upstream's seeded K2 database
already carries `CAppManager|Client` at the same `0x4` we use), and
resource-derived values need re-deriving from K2's own resources. See
`docs/llm-docs/CLAUDE.md` (KOTOR 2 portability section) and
`archiev/refactoring/reports/phase-2-cleanup.md`.

## Split-integrity guarantee

The move was verified mechanically, not by eye: all 367 `constexpr`/`const`
declarations match before and after on **type, name and value**; the 15
typedefs and 3 structs are byte-identical; and every non-blank source line is
accounted for in the new files exactly once. The only two lines that changed
are a stale in-file cross-reference (`"line ~526"`) that the move invalidated.

Note for anyone re-running the report's own verification recipe: its grep
counts 358, not 367 — it does not match multi-word types, so it silently skips
the nine `constexpr unsigned char kActionType*` constants.
