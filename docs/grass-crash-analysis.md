# KOTOR 1 grass crash — analysis

Crash-to-desktop in grassy areas (the Taris Undercity is the one players report
most). This note records what we established from one complete crash bundle plus
decompilation of the engine's grass renderer, what we ruled out, and what is
still open.

Written to be handed to the wider modding community. It is deliberately explicit
about the boundary between confirmed and unconfirmed — the headline mechanism is
**not** fully pinned, and the parts that look like an obvious answer do not
survive their own arithmetic. If you have been investigating this longer than we
have, the "Open questions" section is where the useful disagreement lives.

Binary: KOTOR 1, GOG/Steam build, `swkotor.exe` image base `0x00400000`.
Symbols and decompiles are from Lane Dibello's Ghidra database
(`k1_win_gog_swkotor.exe`).

## Evidence base

One crash bundle from a beta tester of the *Voice of the Old Republic*
accessibility mod, containing a WER minidump paired with the patch log of the
session that produced it:

- `swkotor.exe.3480.dmp`, written 2026-08-15T23:56:36Z
- The mod's own log for that session, which dumps the player's `swkotor.ini` at
  startup and timestamps every area change

The player entered `tar_m04aa` (Taris — Undercity) at 23:41:37Z and crashed at
23:56:29Z, roughly 15 minutes in, with no area change between. Their
`[Graphics Options]` had `Grass=1`, `Disable Vertex Buffer Objects=1`,
`Frame Buffer=0`, `V-Sync=1`.

The machine had Intel integrated graphics (`ig9icd32.dll`, the Intel OpenGL
ICD). The same tester had accumulated ten `swkotor.exe` minidumps in three days;
only this one was analysed.

## Confirmed: the faulting stack

Exception `0xC0000005`, access violation **reading** `0x13db4000`.
`EAX` held the faulting address exactly.

Frames, innermost first, resolved against the dump's module list:

- Frames 0–9 — `ig9icd32.dll` (Intel OpenGL driver), EIP at `+0x8f0b35`
- Frame 10 — `opengl32.dll` `+0x37335`
- Frame 11 — `swkotor.exe` `0x004263ae` = `GLRender::DrawLightmappedGrass+0x17e`

So: the game calls into OpenGL from its grass draw, and the driver faults ten
frames deep while consuming what it was handed.

The faulting address is exactly 4 KB page-aligned — the signature of a read that
walked off the end of a block into the next, unmapped page, rather than a wild
pointer.

## Confirmed: the grass vertex buffer layout

`CAurTriangleBin::CreateArrays` @ `0x004a87c0` allocates one buffer for the
whole bin:

```c
T      = this->field6_0xc;                    // total grass QUADS in this bin
stride = (ushort)this->field0_0x0;            // 0x20, 0x24 or 0x28 — set by BuildGrassPolys
buf    = operator_new(stride * T * 4);        // one allocation, 4 vertices per quad
this->field17_0x38 = buf;
this->field20_0x44 = T * 0x30;                // NOTE: an integer OFFSET, not a pointer
```

Positions are written at `base + i*0x30` with the quad's four vertices at
`+0x00`, `+0x0c`, `+0x18`, `+0x24` — i.e. 4 vertices × 12 bytes = `0x30` per
quad. Attributes (normal, two texcoord sets, optionally a packed colour) are
written after them as `(stride - 0xc)`-byte records.

That gives the layout:

- `[0, T*0x30)` — positions, `T*4` vertices × 12 bytes
- `[T*0x30, T*4*stride)` — attributes, `T*4` records × `(stride - 0xc)` bytes

The two add up to exactly `T * 4 * stride`, matching the allocation. **The
allocation is correctly sized for `T` quads.** `field20_0x44 = T * 0x30` is the
engine caching the position-block size, i.e. the offset at which attributes
begin.

`BuildGrassPolys` @ `0x004a9630` picks the stride: `0x28` when flag `8` is set,
`0x24` when flag `0x10` is set, `0x20` otherwise.

## Confirmed: the draw always uses client-side arrays

`GLRender::DrawLightmappedGrass` @ `0x00426230`:

```c
uVar1 = AurVertexBufferObjectARBAvailable();
if (uVar1 != 0) { (*glBindBufferARB)(0x8892, 0); }   // GL_ARRAY_BUFFER <- 0, i.e. UNBIND
glVertexPointer(3, GL_FLOAT, param_2, param_1);
pvVar3 = (void *)((int)param_1 + param_6 * 0x30);
glNormalPointer(GL_FLOAT, param_4, pvVar3);
glTexCoordPointer(2, GL_FLOAT, param_4, pvVar3 + 0xc);
/* ... unit 1 ... */
glTexCoordPointer(2, GL_FLOAT, param_4, pvVar3 + 0x14);
glDrawArrays(GL_QUADS, param_5, param_6 * 4);
```

Both branches are identical except that the VBO branch explicitly binds buffer
zero first. Grass geometry is therefore **always** submitted as client-side
vertex arrays, read by the driver directly out of process heap at draw time.

Practical consequence: the `Disable Vertex Buffer Objects` ini setting does
**not** change where the driver reads grass from, and is not a factor in this
crash. (We initially suspected it and were wrong.)

Parameters, from the call sites: `param_1` = position base, `param_2` = position
stride (`0xc`), `param_3` = passed but **unused**, `param_4` = attribute stride
(`stride - 0xc`), `param_5` = `first` vertex, `param_6` = quad count.

## Confirmed: a real defect in the call sequence

`CAurTriangleBin::RenderGrassPolys` @ `0x004a6d30` has two ways of issuing the
draw.

Single call, when the bin needs no per-texture splitting:

```c
DrawLightmappedGrass(<base>, 0xc, <&field20_0x44>, iVar8, 0, this->field6_0xc);
//                                                        first=0  count=T
```

Loop, one call per sub-bin (one per grass texture / lightmap pair in the area) —
this is the path taken when multitexturing or ATI fragment shaders are
available, i.e. on any modern GPU:

```c
iVar6 = 0;                                     // running vertex offset, GLOBAL
iVar10 = 0;
do {
    /* bind this sub-bin's textures */
    DrawLightmappedGrass(<base>, 0xc, <&field20_0x44>, iVar8,
                         iVar6,                                  // first
                         (this->field25_0x58).data[iVar10]);     // count, THIS sub-bin only
    iVar6 += (this->field25_0x58).data[iVar10] * 4;
    iVar10++;
} while (iVar10 < (this->field25_0x58).size);
```

The defect: `DrawLightmappedGrass` derives the attribute base as
`param_1 + param_6 * 0x30`. Per the confirmed layout above, attributes begin at
`T * 0x30` — so that expression is only correct when **`param_6 == T`**.

That holds for the single call, where `param_6` is `field6_0xc` (the total). It
does **not** hold in the loop, where `param_6` is one sub-bin's quad count and is
strictly smaller than `T` whenever there is more than one sub-bin. The attribute
base then lands *inside the position block*, and the driver is handed normals and
texture coordinates read from position floats.

This also explains why `param_3` exists and is ignored: the caller passes
`&this->field20_0x44`, the field holding the correct `T * 0x30` offset, and the
function recomputes it from `param_6` instead of using it. The parameter is
vestigial — the fix the original authors apparently intended is sitting unused in
the signature.

This is a genuine, reproducible-by-inspection bug. Every KOTOR 1 install with
more than one grass sub-bin in view renders grass with garbage normals and
texcoords.

## Open question: this defect does not, by itself, explain the crash

We could not make the arithmetic produce an out-of-bounds access, and we want to
be explicit about that rather than ship a satisfying story.

Let `n` = this sub-bin's quads, `f` = `first` = 4 × (quads in preceding
sub-bins), `C` = cumulative quads through this sub-bin, so `f + 4n = 4C` and
`C ≤ T`.

Highest attribute byte the driver reads:

```
base + 0x30*n + (4C - 1) * (stride - 0xc)
```

Buffer end is `base + 4*T*stride`. Since `n ≤ T` and `C ≤ T`, the read stays
inside the allocation for every stride value the engine uses — we checked
`0x20`, `0x24` and `0x28`. Position reads are bounded the same way: the highest
is `base + 12*(4C-1) + 12 ≤ base + 0x30*T`.

So the wrong attribute base reads *wrong data*, but in-bounds data. Something
else is producing the page fault. Candidates we have not eliminated:

- A sub-bin count in `field25_0x58` that exceeds `T`, or a `first` that
  accumulates past `4T` — i.e. the sub-bin list disagreeing with `field6_0xc`.
  This would break the bound directly and is the most likely single answer.
- The buffer being freed or rebuilt (`DestroyGrassPolys` @ `0x004a8460`,
  `CreateArrays`) between the pointer handoff and the driver's read. The grass
  arrays are also mutated per-frame by `UpdateGrassForWind` @ `0x004a68b0`.
- The driver over-reading its own staging copy rather than the game's array —
  see below.

`CAurTriangleBin` is a Ghidra **PlaceHolder Structure** in Lane's database: every
member is an untyped `undefined4`, so field names and offsets are inferred, not
known. Ghidra's rendering of the three call sites is internally inconsistent —
two pass `&this->field19_0x40` (the address of a struct field) where the loop
passes `this->field17_0x38` (the buffer pointer stored by `CreateArrays`).
Anyone continuing this should re-type the struct before trusting per-field
reasoning, ours included.

## Observation: what the memory below the fault actually contains

Reading the dump at the page immediately below the faulting address
(`0x13db3f80`, 128 bytes) does not show float vertex data. It shows a repeating
token-prefixed record structure containing **big-endian** IEEE floats:

```
04 04 42 4b 7a e1   ->  50.87
04 04 40 40 00 00   ->   3.00
04 04 43 57 2b 85   -> 215.17
04 04 43 7b 51 ec   -> 251.32
```

Those are KOTOR world-space coordinates in the right range for the Undercity —
the player was at `(254.58, 204.72)` and a nearby creature at
`(256.31, 210.95, 3.00)`, and `3.00` is the floor height. This looks like the
Intel driver's own command/push buffer, holding grass vertices it has already
batched, byte-swapped into its hardware command format.

If that reading is right, the fault is the driver walking off the end of a
staging buffer it built, and the game's role is having told it to draw more than
it supplied. We did not confirm the buffer's ownership or extent, so treat this
paragraph as a lead rather than a finding.

## Workaround

Set `Grass=0` under `[Graphics Options]` in `swkotor.ini`.

`CClientOptions::SetGrass` @ `0x0061d6f0` writes the option and calls
`AurEnableGrass` @ `0x0044ee30` / `AurDisableGrass` @ `0x0044ee40`, which set the
global `enableGrass` @ `0x0078e3ac`. This is the engine's own shipped graphics
option, exposed as a checkbox in the in-game graphics menu
(`CSWGuiOptionsGraphics::OnGrass` @ `0x006dee00`).

Caveat worth stating: we traced the option to `enableGrass` but did **not** trace
which caller reads that global to gate the render path — neither
`RenderGrassPolys` nor `BuildGrassPolys` reads it, so the gate is further up.
The workaround rests on the option being the engine's own documented control,
not on a traced code path.

The *Voice of the Old Republic* accessibility mod applies `Grass=0` on both games
from v0.7.7 onward, because its players are blind or visually impaired and grass
is purely decorative for them — a free trade there, and emphatically not a
recommendation that sighted players should give up grass to avoid an
intermittent crash.

## What would settle it

In rough order of value:

1. Re-type `CAurTriangleBin` from the constructor @ `0x004a82b0` and
   `AddTriangle` @ `0x004a7860`, then re-read the three `DrawLightmappedGrass`
   call sites. This resolves the `&field19_0x40` vs `field17_0x38`
   inconsistency, which currently blocks exact reasoning.
2. Instrument or breakpoint `RenderGrassPolys` in an affected area and record
   `field6_0xc` (`T`), `field25_0x58.size`, and each sub-bin count. If the
   sub-bin counts sum to more than `T`, the bound is broken and the question is
   answered.
3. Confirm whether the Undercity actually produces multiple sub-bins. If it
   produces exactly one, the loop is never taken there and this whole line of
   investigation is aimed at the wrong function.
4. Collect dumps from non-Intel GPUs. A driver that maps pages differently would
   render garbage silently instead of faulting, which would explain why this is
   reported so unevenly.

## Reproduction notes

Not reliably reproducible on demand. It depends on heap layout at the moment of
the draw, which is why the same save can survive many passes through an area and
then fault. Ten dumps in three days on one affected machine; zero on the
maintainer's own (NVIDIA) machine across months of testing.
