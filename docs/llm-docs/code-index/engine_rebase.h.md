# engine_rebase.h (56 lines)

Engine-address rebasing for non-reference builds of swkotor.exe (e.g. the
Allard Russian translation, whose functions are byte-identical to the
reference Steam/GoG 1.0.3 build but linked at different offsets, -320..+640
bytes, non-monotonic). Every hardcoded engine address in the patch is
written once against the reference build and passed through `R()`; on the
reference build R() is identity, on a known other build it maps through a
generated table. Only `.text` addresses need this — `.data` is byte-stable,
`.rdata` is handled by a separate generated table since sigscan can't place
non-code bytes.

## Declarations (in source order)

- L36 — `namespace acc::addr`
- L41 — `uintptr_t R(uintptr_t referenceVa)`
  note: returns 0 if the running build is known-rebased but the address isn't in its table — a null-pointer call faults recognisably; a stale address would silently corrupt state instead.
- L45 — `bool IsRebased()`
- L49 — `const char* ActiveBuildName()`
  note: "reference", "allard-1.72", or "unknown"; stable for process lifetime.
- L54 — `inline bool Ok(uintptr_t va) { return va != 0; }`
  note: guard for call sites using an address sigscan could not place on a rebased build.
