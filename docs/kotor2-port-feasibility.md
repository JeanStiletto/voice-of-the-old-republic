# KOTOR 2 port — feasibility check (SUPERSEDED)

**Status: SUPERSEDED (2026-07-31) by `docs/kotor2-port.md`,** which is the live
plan. Read this file for the sigscan measurement and why it will not improve —
that part still holds and is still referenced from `kdev-design.md`. Do **not**
read its cost estimate as current: "the KOTOR 2 address database is fresh
reverse-engineering, budget it as such" predates the discovery that the K2 exe
ships full RTTI, which resolves all 392 vtables by class name automatically.

Originally written 2026-07-25 as a side-step while building the KOTOR 1
multi-exe support, to find out whether that work could double as a head start on
a KOTOR 2 port. It cannot — that conclusion is unchanged.

## The question

Could our existing addresses and hooks be carried to KOTOR 2 more or less
mechanically — same engine family, same game design — ideally ending with one
mod serving both games?

## The measurement

`kdev sigscan` builds relocation-tolerant byte signatures from a known-good
KOTOR 1 image and locates them in another executable. Run against two targets
with the identical reference and code path:

- **Allard Russian `swkotor.exe`** (a different build of the same BioWare
  source): **212 of 216** `.text` addresses resolved uniquely, `hooks.toml`
  cross-check 25/25.
- **KOTOR 2 `swkotor2.exe`** (Steam): **0 of 213**. Not one match.

## Why it is zero, and why that will not improve

- Steam's KOTOR 2 is the **Aspyr 2015 rebuild** (PE timestamp 2015-09-23), not
  Obsidian's 2004 binary. So it is not "the same engine nine months later" — it
  is an eleven-year-later recompile by a third studio.
- `.text` is **70% larger**: 0x5846A8 against KOTOR 1's 0x33C000.
- Signature matching finds *the same compiled bytes relocated*. These functions
  were **re-emitted**, not moved. No amount of additional operand wildcarding
  recovers that — it is a property of the problem, not a tuning parameter.

Targeting Obsidian's original 2004 build instead would likely score above zero,
but that is not the binary Steam ships, so it is not the binary users have.

## What this means for a port

The architectural conclusion survives; only the expected discount disappears.

- **`sigscan` contributes nothing to a KOTOR 2 port.** Its value is confined to
  KOTOR 1 variants (Allard, GoG re-packs, future re-releases), where it works
  very well.
- **The seam that makes one mod for two games possible is name lookup, not
  signature matching.** KPatchManager already ships it:
  `GameVersion::GetFunctionAddress(class, function)`,
  `GetGlobalPointer(name)`, `GetOffset(class, member)`, selected by executable
  SHA-256, with `GetTitle()` distinguishing the two games and
  `HasFunction()` / `HasOffset()` / `HasClass()` for graceful degradation where
  KOTOR 2 genuinely differs. We currently use none of it — all 264 addresses
  are hardcoded.
- **The KOTOR 2 address database is fresh reverse-engineering.** Budget it as
  such.
- **Expect struct offsets to dominate that cost, not function addresses.**
  `engine_offsets.h` is largely field offsets, and Obsidian's additions plus a
  decade of Aspyr changes will have moved many. Offsets cannot be
  signature-matched even in principle.

## If this is picked up again

1. Migrate to `GameVersion` name lookup first (tracked separately) — it is
   worth doing on its own merits and is a hard prerequisite here.
2. Verify the migration by generating the KOTOR 1 Steam database from the
   current constants and asserting every name resolves to the value it
   replaced, before launching the game once.
3. Only then start populating a KOTOR 2 database.

Useful detail for later: the KOTOR 2 executable is **not** SteamStub-encrypted
(no `.bind` section), so unlike KOTOR 1 it can be read straight off disk — no
`kdev dump-text` step is needed to get a byte reference.

## Sources

- Tool and its measured boundary: `docs/kdev-design.md`, `kdev sigscan` entry.
- KOTOR 1 multi-exe results: `docs/translation-additions.md`.
