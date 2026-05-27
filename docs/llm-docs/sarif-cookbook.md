# SARIF Query Cookbook

Recipes for querying Lane's Ghidra SARIF export (`docs/llm-docs/re/k1_win_gog_swkotor.exe.sarif`, 490 MB) with `jq`.

This is the data the SQLite address DB (`third_party/Kotor-Patch-Manager/AddressDatabases/kotor1_0_3.db`) doesn't have: full function signatures with parameter types, vtable layouts, every cross-reference (~369k of them), every datatype, and Lane's textual comments.

## Setup

- `jq` is installed at `C:\Users\fabia\bin\jq.exe` (on PATH). Tested with version 1.8.1.
- The SARIF file is at `docs/llm-docs/re/k1_win_gog_swkotor.exe.sarif`.
- All queries below run in 5–10 seconds. The whole file is parsed every time; if you need fast repeated lookups, build the SARIF→SQLite ingester instead (deferred work, not started yet).

## Address conversion

The SARIF stores **incoming** addresses (locations where something happens) as decimal integers, and **outgoing** addresses (xref targets) as 8-character lowercase hex strings. So:

- For `locations[0].physicalLocation.address.absoluteAddress` — use decimal: `printf '%d' 0x418960` → `4295008`
- For `properties.additionalProperties.to` — use hex string: `"00418960"` (note: padded to 8 chars, lowercase)

The recipes below use bash to compute decimal addresses on the fly with `--argjson`.

## Recurring boilerplate

Every recipe runs against the SARIF and pipes through `.runs[0].results[]`. Set this once per shell session:

```bash
SARIF="C:/Users/fabia/Dev/kotor/docs/llm-docs/re/k1_win_gog_swkotor.exe.sarif"
```

## Recipe 1 — Find a function by name

Returns every overload across every class. Useful when you don't know which class implements a method.

```bash
jq -r '
  .runs[0].results[]
  | select(.ruleId == "FUNCTIONS")
  | select(.properties.additionalProperties.name == "HandleFocusChange")
  | .properties.additionalProperties
  | "\(.namespace // "Global")::\(.name)  @\(.location)  \(.value)"
' "$SARIF"
```

Sample output:

```
CSWGuiControl::HandleFocusChange  @00418960  void __thiscall HandleFocusChange(int param_1)
CSWGuiEditbox::HandleFocusChange  @0041a820  undefined __thiscall HandleFocusChange(int param_1)
```

Replace `HandleFocusChange` with any function name. Use `contains("Focus")` instead of `==` for substring search.

## Recipe 2 — List every function in a class or namespace

Useful when planning hooks: see what the class can do at a glance.

```bash
jq -r '
  .runs[0].results[]
  | select(.ruleId == "FUNCTIONS")
  | select(.properties.additionalProperties.namespace == "CSWGuiMainMenu")
  | .properties.additionalProperties
  | "\(.location)  \(.name)\t\(.value)"
' "$SARIF"
```

Sample output:

```
0067ac10  Draw                  undefined __thiscall Draw(float param_1)
0067afb0  OnNewGamePicked       void __thiscall OnNewGamePicked(CSWGuiControl * param_1)
0067b380  HandleInputEvent      undefined __thiscall HandleInputEvent(int param_1, int param_2)
0067b450  OnEnterButton         undefined __stdcall OnEnterButton(int param_1)
0067b9d0  OnModulePicked        void __thiscall OnModulePicked(CSWGuiControl * param_1)
...
```

The `value` column is Ghidra's full signature including parameter types — far richer than the address DB's `param_size_bytes` integer.

## Recipe 3 — Get the function at a specific address

Inverse lookup: "what is `0x67b380`?"

```bash
jq -r '
  .runs[0].results[]
  | select(.ruleId == "FUNCTIONS")
  | select(.properties.additionalProperties.location == "0067b380")
  | .properties.additionalProperties
  | "\(.namespace // "Global")::\(.name)  \(.value)"
' "$SARIF"
```

## Recipe 4 — Find callers of a function (xrefs TO)

"Who calls `HandleFocusChange`?" — use the **hex** form since `to` is a hex string. Callers' addresses come back as decimal `from`; convert with `printf '%x'` for human reading.

```bash
TARGET_HEX="00418960"
jq -r --arg target "$TARGET_HEX" '
  .runs[0].results[]
  | select(.ruleId == "REFERENCES")
  | select(.properties.additionalProperties.to == $target)
  | "from=\(.locations[0].physicalLocation.address.absoluteAddress)  kind=\(.properties.additionalProperties.kind)"
' "$SARIF"
```

Sample output (decimal `from`):

```
from=4302949  kind=UNCONDITIONAL_CALL    # 0x41a8a5 — CSWGuiEditbox override calling base
from=7595268  kind=DATA                  # vtable entry of some GUI class
from=7595420  kind=DATA                  # another vtable entry
...
```

`kind=DATA` results are usually vtable entries — every class that inherits from `CSWGuiControl` has a vtable slot pointing at this function (or its override). `kind=UNCONDITIONAL_CALL` / `CONDITIONAL_JUMP` / `COMPUTED_CALL` are real call sites.

## Recipe 5 — Find what a function calls (xrefs FROM a range)

"What does `HandleInputEvent` call into?" — addresses inside the function body reference other code/data.

```bash
LO=$(printf '%d' 0x0067b380)
HI=$(printf '%d' 0x0067b42f)   # function end (from Ghidra's CODE_BLOCK end + 1)
jq -r --argjson lo "$LO" --argjson hi "$HI" '
  .runs[0].results[]
  | select(.ruleId == "REFERENCES")
  | select(.locations[0].physicalLocation.address.absoluteAddress >= $lo)
  | select(.locations[0].physicalLocation.address.absoluteAddress < $hi)
  | "\(.locations[0].physicalLocation.address.absoluteAddress) -> \(.properties.additionalProperties.to)  (\(.properties.additionalProperties.kind))"
' "$SARIF"
```

Combine with Recipe 3 to resolve each `to` address into a function name.

## Recipe 6 — Comments inside a function

Lane's textual annotations for a given address range. Most functions have no comments; the ones that do are usually load-bearing.

```bash
LO=$(printf '%d' 0x00418960)
HI=$(printf '%d' 0x004189c3)
jq -r --argjson lo "$LO" --argjson hi "$HI" '
  .runs[0].results[]
  | select(.ruleId == "COMMENTS")
  | select(.locations[0].physicalLocation.address.absoluteAddress >= $lo)
  | select(.locations[0].physicalLocation.address.absoluteAddress < $hi)
  | "\(.locations[0].physicalLocation.address.absoluteAddress) [\(.properties.additionalProperties.kind)] \(.properties.additionalProperties.value)"
' "$SARIF"
```

`kind` is one of `pre`, `post`, `end-of-line`, `plate`, `repeatable`. `value` is the comment text.

## Recipe 7 — Get a struct's field layout

Vtable layouts and member offsets in one query. Replace the name with any class/struct from the address DB.

```bash
jq -r '
  .runs[0].results[]
  | select(.ruleId == "DATATYPE")
  | select(.properties.additionalProperties.name == "CSWGuiControl")
  | .properties.additionalProperties.fields // {}
  | to_entries[]
  | "\(.value.offset)\t\(.key)\t\(.value.type.name // .value.type.kind)"
' "$SARIF" | sort -n -k1
```

Sample output (subset):

```
0       vftable     pointer
4       m_someField int
124     HandleFocusChange   void *
128     GetIsSelectable     void *
...
```

Vtable function-pointer offsets are at fixed positions per class — handy for runtime dispatch ("read `*(this + 0x7c)` to get the focus handler of any control").

## Recipe 8 — Find any symbol by name

Catches functions, globals, locals, parameters — anything Lane named.

```bash
jq -r '
  .runs[0].results[]
  | select(.ruleId == "SYMBOLS")
  | select(.properties.additionalProperties.name == "g_pServerExoApp")
  | .properties.additionalProperties
  | "\(.location // .namespace // "?")  \(.name)  kind=\(.kind)"
' "$SARIF"
```

## Recipe 9 — Bookmarks (Lane's "look at this" flags)

```bash
jq -r '
  .runs[0].results[]
  | select(.ruleId == "BOOKMARKS")
  | "\(.locations[0].physicalLocation.address.absoluteAddress)  \(.properties.additionalProperties.category // "?")  \(.properties.additionalProperties.description // "")"
' "$SARIF" | head -40
```

3,828 entries; categories include `Analysis`, `Note`, etc.

## Recipe 10 — Counts by ruleId (sanity check / data overview)

```bash
jq -r '.runs[0].results[].ruleId' "$SARIF" | sort | uniq -c | sort -rn
```

Expected output:

```
369015 REFERENCES
 97601 SYMBOLS
 38409 DEFINED_DATA
 25407 CODE
 24242 FUNCTIONS
  5305 EXT_LIBRARY
  3828 BOOKMARKS
  2287 COMMENTS
  2262 DATATYPE
   ...
```

## When to reach for the SARIF vs the SQLite DB

- **Address DB (`kotor1_0_3.db`)** — fast, small, curated. Use for: known-good function addresses, struct member offsets, global pointers. Most day-to-day queries.
- **SARIF** — slow, large, comprehensive. Use for: parameter types, full signatures, vtable layouts with field names, cross-references, Lane's comments, symbols beyond the curated set, datatype definitions.

If you find yourself running the same SARIF query repeatedly, that's the signal to either (a) cache the result alongside our docs or (b) build the SARIF→SQLite ingester. Until then, 7 seconds per query is acceptable.

## When to reach for Ghidra (the `.gzf`) instead

- **Decompiled C-like pseudocode** — SARIF doesn't include it; you have to run Ghidra's decompiler.
- **Raw instruction bytes** — also not in SARIF; use `tools/ghidra-scripts/DumpBytes.java`.
- **Interactive exploration / following references visually** — a GUI Ghidra session is faster than 30 jq queries.

The SARIF, the address DB, the Ghidra `.gzf`, and Lane's `.h` file each cover different slices of the same RE work. There's no single tool that has everything.
