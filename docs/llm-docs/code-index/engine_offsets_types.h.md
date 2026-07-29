# engine_offsets_types.h (62 lines)

Part of the `engine_offsets.h` family (see that entry for the family map).
Holds the three plain C++ struct layouts the rest of the patch reads engine
memory through, and nothing else — no addresses, no offsets, so it has no
dependency on `engine_rebase.h`.

Also the canonical home for the split's rationale: the header comment explains
the four-way cut and why the axis is portability rather than subsystem. Read it
before adding a constant anywhere in the family.

## Contents

- **`CExoArrayList`** — the engine's pointer-array container:
  `data` / `size` / `capacity`. The shape behind every `*ControlsOffset` and
  the feat/effect-icon lists.
- **`Vector`** — Aurora 3D vector. Right-handed, Z-up; +X = east, +Y = north,
  1.0 unit ≈ 1 metre; bearing 0° = +X, CCW positive. That frame note is the
  reason this struct is documented rather than just declared.
- **`CExoString`** — heap `c_string` + explicit `length`. Most engine string
  accessors construct one in place into caller-supplied storage; the ownership
  rules (we deliberately leak rather than cross the CRT boundary) live with the
  accessors in `engine_offsets_addresses.h`.
