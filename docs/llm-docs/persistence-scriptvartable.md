# Persistence via ScriptVarTable — engine-confirmed reference

Engine-confirmed (decompiled) model of how KOTOR 1 stores per-object named local
variables and round-trips them through the save game. This is the reusable
primitive for **any mod feature that must persist data into the player's save**
(discovery index, custom flags, per-area state) without an external sidecar file.

Produced 2026-06-14 from Lane's GOG SARIF + headless Ghidra decompiles. Addresses
are GOG/Steam `swkotor.exe` and paste straight into `hooks.toml` (see
`project_ghidra_gog_steam_bytes_match`).

Related: `engine-objects-and-architecture.md` (client/server split, object reach),
`ingame-screens-reference.md` (map pins), `feedback_thiscall_int_param_calling_convention`
(the calling-convention trap that bites every direct engine call).

---

## TL;DR — the primitive

- Every server object (`CSWSObject` and everything that derives from it, including
  the player `CSWSCreature`) embeds a **named, typed variable table**
  (`CSWSScriptVarTable`) at object **+0x100** (Ghidra mislabels this field
  `field54_0x100`; the symbol `script_var_table_2` at +0x110 is a *different*
  table — see "Where the table lives" below).
- It supports int / float / **string** / object / location vars, keyed by name.
- It serializes into the `.sav` automatically: `CSWSObject::SaveObjectState`
  writes it, `CSWSObject::LoadObjectState` reads it. **No save hook required** —
  we mutate the in-memory table and the engine's normal save persists it; on load
  the table is repopulated before we'd ever query it.
- To use it from our DLL: reach the **server-side** player creature, build two
  `CExoString`s (name, value), and call `SetString`/`GetString`/`SetInt`/`GetInt`
  on `creature + 0x100`.

Worked precedent in the shipping game: personal map pins (`NW_MAP_PIN_*` — strings)
and journal entries (`NW_JOURNAL*` — ints) both live here. Map-pin names are
strings and they survive save/reload, which is independent proof the named table
persists.

---

## Two distinct local-variable systems (do not confuse them)

KOTOR has **two** unrelated "local variable" tables. Picking the wrong one is the
main trap.

### 1. `CSWVarTable` — fixed, index-keyed, NWScript-facing (NOT ours)
- Struct (20 bytes): `ulong local_booleans[3]` (96 bit-flags) + `byte local_numbers[8]`.
- Indexed by integer slot, holds only booleans and small numbers. **No strings.**
- This is what NWScript `GetLocalBoolean`/`SetLocalBoolean`/`GetLocalNumber`/
  `SetLocalNumber` operate on. Confirmed by decompiling the VM command handlers
  `CSWVirtualMachineCommands::ExecuteCommandSetLocalValue` @0x00543130 and
  `ExecuteCommandGetLocalValue` @0x0053b760 — they pop an object id, resolve it via
  `CServerExoApp::GetGameObject`, then dispatch to `CSWVarTable::SetLocalBoolean`
  @0x0059b000 / `SetLocalNumber` @0x0059b0d0 etc.
- Accessors: `CSWVarTable::GetLocalBoolean` @0x0059b000, `SetLocalBoolean`
  @0x0059b040, `GetLocalNumber` @0x0059b0b0, `SetLocalNumber` @0x0059b0d0.
- We do **not** use this. It can't hold strings and is what KOTOR's limited
  NWScript local API maps to.

### 2. `CSWSScriptVarTable` — named, typed, engine-internal (OURS)
- The string-capable table the journal and map pins use. This is the one we want.
- Not exposed through the KOTOR NWScript local-var commands; the engine calls its
  methods directly in C++. We do the same from our DLL.

---

## `CSWSScriptVarTable` struct layout

`CSWSScriptVarTable` (size 0x14):
- +0x00 `CExoArrayList<CSWSScriptVar> vars`
- +0x0c `field1_0xc` — a flag; when nonzero, `SetInt` runs the `NW_JOURNAL*`
  special path (journal state/date routing). Irrelevant for non-journal names.
- +0x10 `field2_0x10`

`CExoArrayList<CSWSScriptVar>`:
- +0x00 `CSWSScriptVar *data`
- +0x04 `int size`
- (+0x08 capacity)

`CSWSScriptVar` (stride 0x10 = 16 bytes):
- +0x00 `CExoString name` (8 bytes here: `char* c_string` @0, `int len` @4)
- +0x08 `ulong type`
- +0x0c `value` (4 bytes)

`value` encoding by `type`:
- type **1 = int** → value holds the int inline
- type **2 = float** → value holds the float inline
- type **3 = string** → value is a **pointer to a heap `CExoString`** (8 bytes,
  allocated by `MatchIndex` via `operator new(8)`); the table owns/frees it
- type **4 = object** → value holds the object id inline (default/invalid =
  `0x7f000000`)
- type **5 = location** → value is a pointer to a heap `CScriptLocation`
  (`operator new(0x18)`)

Entries are keyed by **(name AND type)** together — an int `"FOO"` and a string
`"FOO"` are independent entries.

---

## Where the table lives on objects

- `CSWSObject` **+0x100** — the named, string-capable `CSWSScriptVarTable`
  (embedded by value). Player creature derives from `CSWSObject`, so the
  player's named table is at `creature + 0x100`.

  **OFFSET CORRECTION (2026-06-14, verified in-game):** the SARIF/Ghidra struct
  for `CSWSObject` MISLABELS its fields. The symbol named `script_var_table_2`
  at **+0x110 is actually a `CSWVarTable`** (the fixed, index-keyed, non-string
  table — see §2 below), NOT the string table. The real `CSWSScriptVarTable` is
  the field Ghidra calls `field54_0x100`, at **+0x100**. Authoritative proof:
  `CSWSObject::SaveObjectState` @0x004cec50 and `LoadObjectState` @0x004d1cf0
  call `CSWSScriptVarTable::Save/LoadVarTable` on `&this->field54_0x100`
  (+0x100) and `CSWVarTable::Save/LoadVarTable` on `&this->script_var_table_2`
  (+0x110). Writing the string accessors through +0x110 derefs a bogus array
  pointer and faults with `0xC0000005` inside `MatchIndex` — the symptom that
  caught this. (The earlier "+0x110, SARIF offset 272, PoC verified" claim was
  wrong: it trusted the mislabeled placeholder; the in-game write never
  actually persisted.)
- `CSWSArea` **+0x1f0** `script_var_table` (area-level named table) — note this
  is also a Ghidra placeholder symbol; re-verify against an engine caller before
  relying on it.
- The fixed `CSWVarTable` (booleans + small numbers, no strings) is what sits at
  the mislabeled `script_var_table_2` (+0x110) on the object, and at
  `CSWSArea::script_var_table` / `CSWSModule::var_table` for area/module. Don't
  conflate it with the +0x100 string table.

---

## Accessor API (addresses, signatures, decompiled semantics)

All `__thiscall`, `this` = the `CSWSScriptVarTable*` (i.e. `object + 0x100`).

### `MatchIndex` @0x0059a390 — the core lookup
`CSWSScriptVar* MatchIndex(this, CExoString* name, ulong type, int createIfMissing)`
- Linear scan of `vars`; returns the entry whose `type` and `name` both match.
- `createIfMissing == 0` and not found → returns NULL.
- `createIfMissing == 1` and not found → appends a new entry (allocating the heap
  `CExoString` for type 3 / `CScriptLocation` for type 5) and returns it.

### `SetString` @0x0059a8e0
`void SetString(this, CExoString* name, CExoString* value)`
- `MatchIndex(this, name, 3, 1)` then `CExoString::operator=(entry->value, value)`.
- Copies `value` into the table-owned string. We pass two `CExoString*`; the table
  manages memory.

### `GetString` @0x0059a590
`CExoString* GetString(this, CExoString* outReturn, CExoString* name)`
- MSVC by-value return: the hidden `outReturn` pointer is the **first** stack arg,
  `name` second. Returns `outReturn`.
- `MatchIndex(this, name, 3, 0)`; if missing, `outReturn` stays the **empty string**
  it was constructed as. So reading an absent var is safe and yields `""`.

### `SetInt` @0x0059a6f0
`void SetInt(this, CExoString* name, int value, int journalArg)`
- `MatchIndex(this, name, 1, 1)` then stores `value` inline.
- `journalArg` (4th param) is consumed **only** when `field1_0xc != 0` and the name
  begins `"NW_JOURNAL"` (routes to `CSWSJournal::SetState/SetDate`). For our own
  names it is ignored. The engine's own load path passes `1`; pass `0` or `1`.

### `GetInt` @0x0059a530
`undefined4 GetInt(this, CExoString* name)`
- `MatchIndex(this, name, 1, 0)`; returns the int, or **0** if missing.

### Others (same pattern)
- `GetFloat` @0x0059a560, `SetFloat` @0x0059a8c0 (type 2)
- `GetObject` @0x0059a620, `SetObject` @0x0059a900 (type 4)
- `GetLocation` @0x0059a650, `SetLocation` @0x0059a920 (type 5)
- `DeleteIndex` @0x0059a4e0, `DestroyString` @0x0059aa60 (cleanup)
- ctor `CSWSScriptVarTable` @0x0059afe0, dtor @0x0059a980

---

## Persistence path (how it reaches the save)

- `CSWSScriptVarTable::SaveVarTable` @0x0059adb0
  `void SaveVarTable(this, CResGFF*, CResStruct*)`
  - Writes a GFF **list field named `"VarTable"`**, one struct per entry with
    fields `Name` (CExoString), `Type` (DWORD), `Value` (INT/FLOAT/CExoString/
    DWORD/struct depending on type).
- `CSWSScriptVarTable::LoadVarTable` @0x0059aa80
  `void LoadVarTable(this, CResGFF*, CResStruct*)`
  - Reads the `"VarTable"` list back and repopulates the table (case 1 → `SetInt`,
    case 3 → MatchIndex+copy string, etc.).

Callers (who drives Save/LoadVarTable):
- `CSWSObject::SaveObjectState` @0x004cec50 calls SaveVarTable (call site 0x4cec6c).
- `CSWSObject::LoadObjectState` @0x004d1cf0 calls LoadVarTable (call site 0x4d1d0c).
- Additional module/area-level callers exist (module IFO / players save, area load).

Because **every** object is saved/loaded through SaveObjectState/LoadObjectState,
the named var table on any persistent object (player creature, module, area)
round-trips through the `.sav` for free. We never touch the save path ourselves.

---

## How to use this from our DLL (implementation spec)

1. **Reach the server-side player creature.** The VM handlers resolve objects via
   `CServerExoApp::GetGameObject(AppManager->server, id)` — the **server** object,
   not the client. The named table is on the `CSWS*` (server) object. Use our
   existing leader/player accessor chain (the combat-announce code reads server
   objects) and make sure we have the server-side `CSWSCreature*`. See
   `engine-objects-and-architecture.md` and `project_object_handle_namespaces`
   (high bit = client; AI/server primitives need server ids).
2. **Form the table pointer:** `table = (CSWSScriptVarTable*)((char*)creature + 0x100)`
   (NOT +0x110 — that's the fixed `CSWVarTable`; writing strings there faults).
3. **Build `CExoString`s** for the variable name and (for strings) the value. Need a
   small construct/assign/destruct helper around the engine's `CExoString` ctor /
   `operator=` / dtor, or replicate the 8-byte `{char* , int}` layout carefully.
4. **Write:** `SetString(table, &name, &value)` / `SetInt(table, &name, v, 0)`.
   **Read:** `GetString(table, &out, &name)` / `GetInt(table, &name)`.
5. **No save/load hook.** Mutating the table is enough; the engine persists it.

### Calling-convention trap (must heed)
`GetString` returns `CExoString` by value → the hidden return-buffer pointer is the
first stack argument. Declare the typedef exactly, or it returns caller-frame
garbage. This is the same class of bug as
`feedback_thiscall_int_param_calling_convention` and the KPatchManager esp+X /
LEA issues — prefer register-source mid-function hooks or a correctly-typed direct
call; verify the first read in-game, not just at clean build.

---

## Verified working (2026-06-14, after the offset correction)

History worth keeping: the original PoC (`scriptvar_poc.cpp`) used **+0x110** and
its "round-tripped through the .sav" claim did **not** hold up — on a real
mid-game save (player table at +0x100 holding 0 string vars; +0x110 a
`CSWVarTable` with a bogus-as-array header `data`/`size`) every `SetString`/
`SetInt` faulted `0xC0000005` inside `MatchIndex`'s append (deref of the bogus
`data` pointer at `data+0x8`). Reads "worked" only because scanning the wrong
table's bytes found no match and returned 0/empty without faulting. Discovery
appeared to work in-session purely from its in-memory mirror; nothing reached
the save.

After correcting the offset to **+0x100** (the real `CSWSScriptVarTable`; see
"Where the table lives"), the in-game self-test passed cleanly:
`SetString`→`GetString` returns `[selftest-ok]`, `SetInt`→`GetInt` returns
`1337`, no SEH faults. This is the offset/recipe the shipping
`engine_scriptvar.*` module uses.

Concrete recipe that works (copy for the real feature):
- `CExoString` built directly via engine ctor `CExoString::CExoString(char*)`
  @0x005e5a90 and freed via `~CExoString` @0x005e5c20; layout `{char* p; int len}`
  (8 bytes). No hand-rolled struct needed.
- `__thiscall` typedefs called by `reinterpret_cast`ing the raw address; `GetString`
  declared `void* (__thiscall*)(table, out, name)` — the by-value-return hidden
  out-pointer is the first stack arg, returned in EAX.
- Anchor: the **player creature** (server-side, from `GetPlayerServerCreature()`).
  Confirmed it persists. Module/area tables also persist but player is cleanest for
  per-playthrough data.

## Still open (feature design, not primitive)

- The discovery index's key encoding and the recording hook point (see the
  discovery-cycling design note) — these sit on top of this now-proven primitive.

---

## Why this matters — the feature it unlocks

This is the enabling primitive for **discovery-driven cycling**: an in-save,
per-map index of objects the blind player has already had narrated, so the middle
tier of object cycling can resurface known objects without map-side spoilers.

Object identity for that index (separate investigation, 2026-06-14): named/unique
objects carry a **stable per-map id** — the GIT `TemplateResRef`, surfaced at
runtime as the object tag (`CSWSObject` tag field). Verified empirically by dumping
Javyar's Cantina (`tar_m03ae`): every story NPC (Mission `tar03_mission031`,
Zaalbar, Calo Nord, Zax, the pazaak seller…) has a unique resref, while anonymous
patrons repeat. So the persistence key is `(area tag, object tag)` for named NPCs
(no position needed), and `(area tag, object tag, north-south ordinal)` for static
objects whose tags repeat. (The throwaway GIT dumper used for that one-off check has
since been removed.)
