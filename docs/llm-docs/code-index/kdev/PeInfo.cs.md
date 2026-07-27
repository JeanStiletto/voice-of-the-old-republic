# PeInfo.cs (136 lines)

Minimal PE32 header reader shared by `DumpTextCommand` and `SigScanCommand`. Deliberately tiny — only image base, section table, link timestamp. Two correctness-critical subtleties documented in the class remarks: (1) `imageAligned` must be passed explicitly by every caller since a memory dump stores sections at `VirtualAddress` while an on-disk file stores them at `PointerToRawData` — getting this wrong silently reads wrong bytes rather than erroring; (2) sections are matched by `span = max(VirtualSize, RawSize)` because the two disagree on this engine's `.text` and using only one drops boundary addresses.

## Declarations (in source order)

- L22 — `sealed record PeSection(Name, VirtualAddress, VirtualSize, RawSize, RawPointer)`
- L29 — `sealed class PeInfo`
- L31-34 — `ImageBase`, `SizeOfImage`, `Timestamp`, `Sections` (required init properties)
- L36 — `DateTime TimestampUtc`
- L44 — `PeInfo Parse(ReadOnlySpan<byte> buf, ulong? imageBaseOverride)` — validates MZ/PE signatures and PE32 (not PE32+) optional-header magic, reads the section table
  note: `imageBaseOverride` lets a caller state the actual mapped base for a memory dump, since header ImageBase and actual base could theoretically differ (they don't for this exe — no ASLR — but the override keeps the dump self-describing)
- L94 — `PeSection? SectionForRva(uint rva)`
- L104 — `PeSection? SectionNamed(string name)`
- L113 — `int VaToOffset(uint va, bool imageAligned)` — returns -1 if outside every section or (file mode) inside unbacked virtual padding
- L125 — `uint OffsetToVa(int offset, bool imageAligned)`
