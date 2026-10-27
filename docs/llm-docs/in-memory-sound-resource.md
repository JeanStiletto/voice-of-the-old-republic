# In-memory sound resources (KOTOR 1 + 2) — can the engine play a WAV that has no file?

**Status 2026-10-27: yes, and it ships.** Route A below is implemented in
`audio_loop.cpp` (`LoopSource::StartFromMemory`) and used by positional combat
speech — see `positional-speech.md` for the feature built on it. Tested end to
end in game on KOTOR 1; the KOTOR 2 values (section at the end) are
decompile-witnessed and built in, not yet tested in game.

Research for "positional speech": render TTS to PCM in memory (Prism
`speak_to_memory`), then play it through the engine's own 3D sound path at a
world position, so it gets the engine's panning, distance falloff, volume
sliders and EAX/dsoal reverb, just like the existing positional cues.

Decompiled 2026-10-27 from Lane's GOG database (bytes match Steam for K1).
Sections 1-6 are KOTOR 1; KOTOR 2 has its own section at the end. Format:
known (seen in a decompile), suspected (inferred), open (not yet checked).

Related: `audio-internals.md` (CExoSoundSource lifecycle, Play3DOneShotSound
gain chain); `patches/Accessibility/audio_loop.cpp` (the mod already owns a
`CExoSoundSource` and reads `.internal` at +0x04).

---

## Short answer

- **Option 2 (in-memory resource): viable.** The engine never needs a file once
  a `CRes` is marked loaded. `CExoResMan::Demand` returns `CRes.data` straight
  away when the "loaded" flag is set, and the sound code passes that pointer to
  Miles as an in-memory WAV image (`AIL_set_3D_sample_file`). A `CResWave` that
  we fill ourselves plays through the normal 3D voice path.
- **Option 1 (new WAV in Override mid-session): not found.** Override is a
  directory key table, scanned once at startup into an in-memory hash table.
  Lookups never touch the disk. A file added later stays invisible unless
  someone calls `CExoResMan::UpdateDirectoryKeyTable("OVERRIDE:")`, which
  rescans the whole folder. The engine does this itself for `SERVERVAULT:`
  after it writes a character file. Overwriting a file that already existed at
  startup does work: its bytes are read by path every time the resource is
  serviced, though a cached copy in memory is served first.
- **The streaming path is different.** `CExoStreamingSoundSource` (used by
  `Play3DOneShotSound` for resrefs starting `C_`/`P_`/`N_`, plus music and voice
  lines) bypasses the resource manager. It `fopen`s
  `streamsounds\<resref>.wav` by path on every play, so a new file there *is*
  found mid-session. But streaming "3D" is only a stereo pan plus distance
  volume (`Update3DStream`), not a Miles 3D voice, and the one-shot streaming
  slot is shared with the engine's own `C_`/`P_`/`N_` sounds. It is not the
  route we want.

---

## 1. How a WAV resource is obtained (KNOWN)

### CResHelper<CResWave,4>::SetResRef @0x005d5e90
Called by `CExoSoundSource::SetResRef @0x005d61c0` (after `ShutDownSource`,
which stops the voice and drops the old demand) and by
`CExoSoundInternal::Play3DOneShotSound @0x005d7680` on a freshly allocated
0xa0-byte `CExoSoundSourceInternal`. The helper's fields are the first fields
of `CExoSoundSourceInternal`: +0x04 `requested`, +0x08 `CRes* resource`,
+0x0c `CResRef`.

1. If the new resref equals the current one (`+0x0c`), it **returns without
   doing anything**. Re-setting the same name keeps the old `CRes`.
2. If a `CRes` is already held, it calls `CRes::CancelRequest` if a request is
   pending, then `CExoResMan::ReleaseResObject`. If that returns 0, it deletes
   the `CRes` through vtable slot 0 (scalar deleting destructor).
3. Resref `""` leaves the helper empty.
4. `CExoResMan::GetResObject(resref, 4)` @0x004074d0. If that returns NULL, it
   does `operator new(0x3c)`, then `CResWave::CResWave`, then
   `CExoResMan::SetResObject(resref, WAV, res)`. **SetResObject only links the
   `CRes` if a key entry exists.** With no key, the new `CResWave` keeps
   id = -1, refcount 0 and no table entry, and it is owned only by this helper.
5. If `param_2 != 0`, it also calls `CRes::Request` (async queue). Both
   `Play3DOneShotSound` and the mod's wrappers pass 0.

### Lookup: CExoResMan::GetKeyEntry @0x00407230
This is a pure in-memory hash lookup (`CExoKeyTable::FindKey @0x0040ec50`:
open addressing, linear probe) over the key-table lists, in this order:

1. Directory tables (`+0x14`)
2. ERF tables with flag 1
3. RIM tables
4. ERF tables with flag 2
5. Fixed (BIF, `chitin.key`) tables

`GetResObject` returns `entry->res` (the shared `CRes`, refcount `+0x1c`
incremented) or NULL. **No disk access.**

### Loading the bytes: CExoResMan::Demand @0x004089f0
`CExoSoundSourceInternal::Play @0x005db4d0` calls
`CExoSoundSourceInternal::Demand @0x005dae50`, which calls `CRes::Demand`,
which calls `CExoResMan::Demand`.

- If id (`+0x08`) is -1, it returns 0. This is why an unresolvable resref is
  silent with no error.
- **If flag 4 ("loaded") is set and `data != NULL`, it returns `data` and
  increments `demands`.** If the `CRes` sits on the release list (flag 0x100,
  demands 0), it is unlinked first. No key table or file is consulted.
- Otherwise it switches on `id >> 30`:
  - 0: `ServiceFromResFile` (BIF)
  - 1: `ServiceFromImage` (RIM)
  - 2: `ServiceFromEncapsulated` (ERF)
  - 3: `ServiceFromDirectory`
- `ServiceFromDirectory @0x004078f0` requires `table_entry != NULL`. It finds
  the directory table whose index matches id bits 14-19, builds the path
  `<table name>\<resref>` with the extension from the entry type, opens it with
  `CExoFile "rb"`, takes the size from the file, calls `CExoResMan::Malloc`,
  reads it, then calls the vtable +0x10 `OnResourceService` hook and sets
  flag 4 if that returns non-zero. **The file is opened by path at service
  time, not at startup.**

### Freeing: Release / ReleaseResObject / Dump / Free / FreeChunk
- `CExoResMan::Release @0x00408c90`: decrements `demands`. At 0 it puts the
  `CRes` on the release list (`CExoResMan+0x20`) and sets flag 0x100. **The
  data stays cached** for reuse.
- `CExoResMan::ReleaseResObject @0x00409cf0`: decrements refcount (`+0x1c`).
  At 0 with `demands == 0` it clears `table_entry->res` and calls
  `Dump @0x00409b40`. Dump calls `Free` if flags & 0x14 and nothing holds a
  demand. It returns 0, so the helper then deletes the `CRes`.
  - Re-checked 2026-10-27: the `table_entry->res = NULL` write is
    null-guarded, so a keyless `CRes` is safe here. Dump's third test reads
    as `fixed_key_table` in Ghidra only because the decompile types its
    `this` as `CExoResMan`; it is `CRes+0x10`, i.e. `data != NULL`, not the
    table entry. A keyless injected `CRes` is therefore freed normally.
- `CExoResMan::Free @0x00407020`: works only if the `CRes` is on the release
  list. It calls vtable +0x0c (`OnResourceFreed`), unlinks it, then calls
  `FreeResourceData`, which does a CRT `_free(data)` and credits the budget
  `CExoResMan+0x08` with `size`. It clears flags 4, 0x10 and 0x100.
- `CExoResMan::FreeChunk @0x004070a0`: evicts release-list entries with
  `demands == 0` until enough budget is free. It is called from
  `Malloc @0x004077b0` when the budget runs out. The budget is total physical
  memory / 2, minimum 16 MB (`CExoResMan` constructor @0x00409bf0), so
  eviction is rare.
- `Malloc` allocates with the game's own `operator new(size)` (+6 or +10 when
  `CRes+0x20` / `+0x24` are set; both are zeroed by the constructor) and
  debits the budget.

---

## 2. Override and directory scanning (KNOWN)

### Startup registration
The unnamed client init function around 0x005f8800-0x005f8d40 (Ghidra folds
it into `CClientExoAppInternal::Uninitialize`'s range) calls
`CExoResMan::AddResourceDirectory @0x00408800` for these names, in order:

- `TEMPCLIENT:`
- `OVERRIDE:` (string @0x0074553c)
- `ERRORTEX:`
- `RIMS:`
- `SERVERVAULT:`
- `PORTRAITS:`
- `HD0:MOVIES`
- `HD0:STREAMWAVES`
- `HD0:STREAMMUSIC`

`HD0:CHITIN` goes through `AddFixedKeyTableFile`. `OVERRIDE:textures` and
`HD0:patch` go through `AddEncapsulatedResourceFile`. `RIMS:GLOBAL` goes
through `AddResourceImageFile`. `streamsounds` is **not** a key table.

### Building a table
`AddKeyTable @0x00406e20` creates the table and calls
`CExoKeyTable::BuildNewTable @0x00410190`, which calls
`AddDirectoryContents @0x0040f200`. That function makes one
`CExoBase::GetDirectoryList` call (a live `FindFirstFileA`/`FindNextFileA`
scan, `CExoBaseInternal::GetDirectoryList @0x005e8cf0`, no cache). It sizes
the hash table to `2*count+1` and `AddKey`s every file whose extension maps to
a resource type. After that, nothing looks at the folder again.

### Rescan at runtime
- `CExoResMan::UpdateDirectoryKeyTable(name) @0x004088e0` calls
  `UpdateKeyTable(name, DIRECTORY) @0x00407fe0`. That compares the name
  case-insensitively with each table's `resource_name` and calls
  `CExoKeyTable::RebuildTable @0x00410260`.
- RebuildTable rescans the folder into a fresh table and moves live `CRes`
  objects across to their new entries. A `CRes` whose file vanished is dumped
  and gets id -1.
- Calling `AddResourceDirectory` again with an existing name also ends in
  RebuildTable.
- **Engine precedent:** `CSWSPlayer::SaveServerCharacter @0x005624c0` writes a
  `.bic` and then calls `UpdateDirectoryKeyTable("SERVERVAULT:")`. Callers of
  `AddResourceDirectory` at load time (LoadModule, LoadGame, save/load GUI)
  handle `MODULES:` / `CURRENTGAME:` and similar names, not Override.

### Answer to question 2 (Override mid-session)
- **A new file in Override is not found.** It becomes visible only after
  `UpdateDirectoryKeyTable("OVERRIDE:")`, which rescans and rehashes the whole
  folder (heavy on large Override folders).
- **A file that existed at startup and is overwritten in place is re-read**
  the next time its `CRes` is serviced. That happens only once the cached copy
  is gone: the refcount must drop to 0 so Dump frees it, or FreeChunk must
  evict it. While any holder keeps the `CRes`, the old bytes are served from
  memory.
- **A new file in `streamsounds\` (or `streamwaves\`) is found** by the
  streaming source, because `CExoStreamingSoundSourceInternal::Play @0x005dca30`
  formats `HD0:STREAMSOUNDS\%s` (or `STREAMWAVES` / `STREAMMUSIC`), resolves
  the alias (`CExoAliasListInternal::ResolveFileName @0x005eb6b0`, string
  building only) and `InitializeSource @0x005dbd30` calls `fopen` /
  `AIL_open_stream`.

---

## 3. Layouts (KNOWN unless marked)

### CRes (0x28; constructor @0x00406460)
- +0x00 vtable
- +0x04 u16 `demands` (active users; while > 0 the data is pinned)
- +0x06 u16 request count (async queue)
- +0x08 key id. -1 means no key. Bits 30-31 are the source class used by
  Demand, GetTable and Release (0 BIF, 1 RIM, 2 ERF, 3 directory). Bits 14-19
  hold the table index (matched against `CExoKeyTable+0x28`).
  - Pitfall: `(id & 0xC0000000) == 0x40000000` makes `Release` call into a
    packed-file vtable.
- +0x0c flags:
  - 0x4 loaded
  - 0x10 async read in flight
  - 0x100 on the release list / delete deferred
  - 0x200 and 0x8 seen in ReleaseResObject / RebuildTable, meaning not pinned
    down
- +0x10 `data`
- +0x14 `CKeyTableEntry* table_entry`
- +0x18 `size`
- +0x1c refcount (holders via GetResObject / SetResObject)
- +0x20 "6-byte prefix" allocation flag
- +0x24 "+10 slack" allocation flag

### CResWave (0x3c; constructor @0x005df1b0)
- Vtable base 0x0074d398 (suspected base; slots confirmed by DATA xrefs):
  - +0x0c `OnResourceFreed` @0x005df1f0 (vtable entry 0x0074d3a4)
  - +0x10 `OnResourceService` @0x005df230 (0x0074d3a8)
  - +0x14 `GetData` @0x005df360 (0x0074d3ac)
- +0x28 "data was allocated by Miles" (decompressed MP3/ADPCM).
  `OnResourceFreed` then frees it with `AIL_mem_free_lock` and nulls `data`,
  so `FreeResourceData` skips the CRT free.
- +0x2c "fake header": `GetData` returns `data + 0x1d6`.
- +0x30 serviced OK.
- +0x34, +0x38: decompression output (suspected pointer and size).

### CExoSoundSourceInternal (0xa0; constructor @0x005daf50)
Already partly in `audio-internals.md`. New fields seen here:

- +0x04 requested
- +0x08 `CRes*`
- +0x0c `CResRef`
- +0x10 `do_3d`
- +0x24 looping
- +0x28 one-shot auto-delete
- +0x2c delete-me
- +0x30 `resource_demands` (0/1)
- +0x38 `C2DVoice*`
- +0x3c `C3DVoice*`
- +0x58 cached duration in ms (the Play return value; zeroed by
  `CExoSoundSource::SetResRef`)

### CExoSoundSource
`.internal` is at +0x04. It is already used and verified in `audio_loop.cpp`.

### How the bytes reach Miles
- `CExoSoundInternal::PlaySourceOn3DVoice @0x005d71c0` calls
  `AIL_set_3D_sample_file(voice, res->GetData())`.
- The 2D path `@0x005d7480` uses `AIL_set_sample_file`.
- Both take a **pointer to a complete in-memory WAV file image**. Miles parses
  the RIFF header itself and keeps the pointer for as long as the voice plays.
  There is no file handle.

---

## 4. Injection candidates

### A. Keyless private CResWave on an owned CExoSoundSource (RECOMMENDED)
This is known from the decompiles; the end-to-end path is untested.

Recipe (main thread only; `CExoResMan` is not thread-safe outside the async
RIM loader's critical section):

1. `CExoSoundSource::SetResRef(src, "<resref with no key anywhere>", 0)`.
   The helper creates a fresh `CResWave` owned only by this source:
   id -1, refcount 0, no table entry.
2. `res = *(CRes**)(internal + 0x08)`.
3. Set `res->size` (+0x18) to the WAV byte count. Call
   `CExoResMan::Malloc(*(CExoResMan**)0x007a39e8, res)` (thiscall, returns 1
   on success). This uses the game's own heap and budget accounting.
4. `memcpy` the RIFF image to `res->data`.
5. Call vtable +0x10 (`OnResourceService`). It returns 1 if `AIL_WAV_info`
   accepts the header.
6. Set flags (+0x0c) `|= 4`.
7. Set id (+0x08) to `0xC0000000`: directory class, not -1, not the
   0x40000000 pattern.
8. `Set3D(1)`, `SetPosition`, `SetPriorityGroup`, `SetVolume`, `Play()` as
   usual.

Lifetime and ownership:
- **The engine owns and frees the buffer, with the same allocator that made
  it** (`operator new` → CRT `_free`).
- After play, `ShutDownSource` releases the demand and the `CRes` joins the
  release list.
- On the next `SetResRef`, or when the source is destroyed,
  `ReleaseResObject` hits refcount 0 → Dump → Free (`OnResourceFreed`, then
  `_free(data)`), and the helper deletes the `CResWave`. Nothing is cached
  under the resref, because no key exists.
- If FreeChunk evicts the data before a replay, Demand falls through to
  `ServiceFromDirectory`, which bails on the NULL `table_entry`. Replay is
  then silent, never a crash.
- Never touch `res->data` while `demands > 0`: Miles reads it from its mixer
  thread.
- Never `free()` it from the mod.
- If the `CRes` was never demanded (never played), Dump finds it off the
  release list and the data leaks once. This is the engine's own behaviour.

Gotchas:
- Re-using the **same** resref on the same source is a no-op (the early-out at
  step 1 of SetResRef). Either `SetResRef("")` first, or alternate names.
- **Mono is mandatory for 3D.** Play aborts (Release, silence) when
  `do_3d && channels != 1`, after it has already stored the duration. So a
  non-zero Play return does not prove the sound is playing; check
  `internal+0x3c` (3D voice) instead.
- Works only with an owned source. `Play3DOneShotSound` runs SetResRef and
  Play inside one call, so there is no point to inject.

Alternative allocator (suspected equivalent): allocate with Miles
`AIL_mem_alloc_lock` (`GetProcAddress` on Mss32.dll; the exe imports only the
free function) and set `CResWave+0x28 = 1`. Then `OnResourceFreed` frees the
buffer with `AIL_mem_free_lock`, which mirrors the engine's decompressed-ADPCM
path. In this case do not call Malloc.

### B. Keyed slot + injection: makes Play3DOneShotSound usable
Known mechanics, but this is a design idea, not tested.

- Give a resref a real key entry by shipping placeholder files at install
  time, e.g. `Override\acc_tts0.wav` … `acc_tts7.wav`.
- At runtime, get or create the shared `CRes` (`GetResObject`; if NULL,
  construct a `CResWave` and call `SetResObject` the way the helper does).
  Hold one refcount ourselves so it is never dumped.
- Before each utterance, when `demands == 0`: `FreeResourceData`, `Malloc`,
  copy, `OnResourceService`, flag 4.
- Then any `Play3DOneShotSound("acc_tts0", pos, …)`, or an engine-created
  source, gets our bytes through `GetResObject` and `Demand`.
- Risks:
  - Shared state: an engine source holding the slot pins it.
  - Override RebuildTable would re-link the slot but keep our `CRes`.
  - If eviction happens, the placeholder file is read instead (harmless).
- Only worth doing if we specifically want `Play3DOneShotSound` semantics
  (fire and forget, auto-delete). Route A is simpler.

### C. Runtime AddKey into the Override table (NOT RECOMMENDED)
- `CExoKeyTable::AddKey @0x0040e990` can add an entry: the table has about
  half its slots free.
- But any `RebuildTable` (our own or the engine's) drops keys whose file is
  missing and sets their `CRes` id to -1.
- It also has to build the encoded id by hand. Placeholder files (B) get the
  same result robustly.

### D. Write file + rescan (Option 1)
- Write to Override, then `UpdateDirectoryKeyTable("OVERRIDE:")`. Rescans the
  whole folder per utterance; works in principle.
- Or overwrite a placeholder and rely on the cache being gone.
- Both add disk round-trips, have races with the async module loader
  (`GetKeyEntry` walks directory tables from `AsyncLoad`), and give nothing
  over A.

### E. Streaming source with a file in streamsounds
- Finds new files, as described above.
- But it gives a pan-only pseudo-3D (`Update3DStream @0x005dc560`: pan from
  listener forward, linear distance volume, a 1/8 rear dip, reverb via
  `AIL_set_stream_reverb_levels`), not a Miles 3D voice. No dsoal HRTF.
- One shared one-shot streaming slot (`CExoSoundInternal+0x148`) means engine
  `C_`/`P_`/`N_` one-shots and our speech cut each other off.
- Rejected for positional speech.

---

## 5. Format constraints (KNOWN unless marked)

- **Plain RIFF/WAVE, header plus data in one contiguous buffer.** Miles parses
  it (`AIL_WAV_info`, `AIL_set_3D_sample_file`).
- **Mono for 3D** (hard check in `CExoSoundSourceInternal::Play`). Stereo is
  allowed only on 2D sources.
- 16-bit PCM is the safe choice. The sample rate is taken from the header and
  used as the playback rate, so 22050 Hz or 44100 Hz both work.
- IMA ADPCM (format 0x11) is decompressed in `OnResourceService` (the
  decompile is garbled around the format test; suspected).
- **Float WAV (format 3): open.** Not seen handled; convert Prism's floats to
  int16.
- MP3 is accepted when wrapped as `"BMU V1.0"` plus an 8-byte prefix
  (`AIL_decompress_ASI`).
- Fake-header quirk: the global at 0x007a2374 is statically 1, with no
  writers. With it set, a buffer that does not start with `RIFF` (or `BMU`) is
  checked for `RIFF` at +0x1d6 (470 bytes), and `GetData` skips that prefix.
  Our buffer starts with `RIFF`, so this path is never taken. The streaming
  reader applies the same 0x1d6 rule to files.
- Resref: up to 16 characters. Avoid a `C_`, `P_` or `N_` prefix: those reroute
  `Play3DOneShotSound` to the streaming path.

---

## 6. Open

- ~~The end-to-end Route A test has not been run.~~ DONE 2026-10-27 on
  KOTOR 1: a synthesised tone, SAPI, OneCore and Eloquence speech all play
  as real 3D voices (`internal+0x3c` set right after Play); 20+ rapid
  create/destroy cycles stay stable and the engine reuses the same buffer
  address each time (freed, no leak). Then the full combat feature.
- Whether this Miles build accepts WAVE_FORMAT_IEEE_FLOAT. (Moot: the mod
  always converts to 16-bit PCM in `audio_wav.cpp`.)
- The exact meaning of CRes flags 0x8 and 0x200.
- Whether the vtable +0x10 `OnResourceService` call needs anything besides
  `data` and `size` (it reads only those in the decompile).
- K2 equivalents: researched 2026-10-27; see the "KOTOR 2" section at
  the end.

---

## Verdict

**Route A is viable and the cleanest choice:** a keyless, private `CResWave`
on a mod-owned `CExoSoundSource`, filled through `CExoResMan::Malloc` and
`OnResourceService`, with flag 4 set and id `0xC0000000`.

- It plays through `PlaySourceOn3DVoice`, so we get Miles 3D, distance falloff
  from the priority group, slider volume and EAX effects level, exactly like
  existing cues.
- **Ownership:** the engine frees the buffer with its own allocator when the
  source is re-pointed or destroyed. Nothing is cached under the resref.
- **Rules:**
  - Main thread only.
  - Mono 16-bit RIFF.
  - Never free or modify the buffer while `demands > 0`.
  - Change the resref (or SetResRef `""`) between utterances.
- **Worst failure modes:**
  - A one-off leak when an injected sound is never played.
  - Silence after a rare eviction.
  - No dangling pointer, as long as the mod never frees the buffer itself.

**Smallest proof of concept (one session):** a debug hotkey in the
`audio_loop` owned-source style.

1. Build a 1-second 440 Hz mono 16-bit 22050 Hz RIFF in memory. No Prism
   needed yet; swap in `speak_to_memory` output (converted to mono int16) as
   step 2.
2. Construct a `CExoSoundSource`.
3. `SetResRef("acc_memtest_a", 0)`.
4. Log the `CRes` pointer, id, refcount, table entry and vtable. Expect -1, 0,
   NULL and 0x0074d398.
5. Run Malloc, memcpy, `OnResourceService` (log its return), then flags
   `|= 4` and id = `0xC0000000`.
6. `Set3D(1)`, a position 3 m to the player's left, the mod's priority group,
   `Play()`.
7. Log the Play return, `internal+0x3c` (must be non-NULL) and `internal+0x30`
   (must be 1).
8. The user reports the direction they hear and how the tone pans as they turn
   and walk away.
9. Repeat 20 times, alternating `acc_memtest_a` and `acc_memtest_b`. Then
   destroy the source and confirm there is no crash and that later sounds
   still play.

---

## KOTOR 2 (Steam/Aspyr) — route A port values

Decompiled 2026-10-27 from the `kotor2` Ghidra project (`swkotor2.exe`,
Steam). Steam addresses only; GOG goes through the existing relink tables.
Format as above: known (seen in a decompile or listing), suspected
(inferred), open. K2 code keeps frame pointers and is mostly unnamed, so
every function below was reached by following calls down from the
facade `CExoSoundSource::SetResRef` @0x0070AF20 (from `audio_bus.h`).

### Short answer (K2)
- **Route A ports unchanged in shape.** Every step of the K1 recipe has a
  K2 twin, and the CRes, CResWave and helper offsets are the same. Only
  two addresses change: the `CExoResMan*` global and
  `CExoResMan::Malloc`.
- No behavioural difference was found that would break route A. The
  differences (see "Differences from K1") are field moves the mod does not
  touch, plus one extra RIM guard in the free path that the 0xC0000000 id
  stays clear of.

### CExoResMan* global (KNOWN)
- **`0x00A1B490`**.
  - KPatchManager's `kotor2_steam_aspyr.db` lists it as
    `EXO_RESOURCE_MANAGER_PTR` = 10597520 (0xA1B490).
  - The K1 row (8010216 = 0x7A39E8) matches the K1 value the mod already
    uses, so the name means the same thing in both databases.
- Code witness: `CResHelper<CResWave,4>::SetResRef` @0x0070A8D0 runs
  `MOV ECX,[0x00A1B490]` before each of its calls to:
  - `ReleaseResObject`, at 0x0070A948
  - `GetResObject`, at 0x0070A9CA
  - `SetResObject`, at 0x0070AA29
- The `CRes::Demand` / `CRes::Release` / `CRes::Dump` wrappers
  (0x007111E0, 0x007112B0, 0x00711230) also load ECX from this global.
- It sits 4 bytes below the `CExoSound*` global 0x00A1B494. K1 has the
  same pairing (0x7A39E8 / 0x7A39EC).

### CExoResMan::Malloc(CRes*) @0x00712F30 (KNOWN)
- **Calling convention:** `__thiscall`. ECX is the `CExoResMan`, there is
  one stack argument (the `CRes*`), and the function ends with `RET 0x4`.
  It returns 1 on success and 0 on failure (`MOV EAX,0x1` /
  `XOR EAX,EAX`).
- **Body, the same as K1:**
  1. While `res->size` (+0x18) is larger than the budget
     (`CExoResMan+0x08`), it calls FreeChunk @0x00712160. It stops when
     FreeChunk frees nothing.
  2. It debits the size from the budget.
  3. It allocates `size` bytes with `operator new` (FUN_00919723). If
     +0x20 is set, it allocates `size + 6` and moves the pointer forward
     by 6. If +0x24 is set, it allocates `size + 10`. The CRes
     constructor zeroes both fields.
  4. If the allocation returns NULL, it credits the budget back and
     returns 0.
- **FUN_00919723 is operator new.** The facade constructor uses it for
  the 0xA8-byte internal, and the helper uses it for the 0x3C-byte
  CResWave.
- **Caller witness:** `ServiceFromDirectory` @0x007133A0 calls Malloc at
  0x007139AB with `MOV ECX,[EBP-0xEC]` (its own `this`). It does this
  after storing the file length in `res+0x18`.

### CRes layout (KNOWN, same as K1)
The constructor is FUN_007110D0 and the vtable is `CRes::vftable`
@0x0099C388.

- +0x04 u16 `demands`.
  - Zeroed by the constructor.
  - Incremented by Demand.
  - Returned by `CRes::GetDemands` @0x00711250.
- +0x06 u16. Zeroed by the constructor.
- +0x08 key id.
  - The constructor sets it to 0xFFFFFFFF.
  - SetResObject copies the key entry's id into it.
- +0x0c flags. The constructor clears bits 4 and 8.
  - 0x4: loaded
  - 0x10: async read in flight
  - 0x100: on the release list
- +0x10 `data`
- +0x14 `table_entry`. Written by SetResObject @0x00712B60.
- +0x18 `size`
- +0x1c refcount. SetResObject sets it to 1; GetResObject increments it.
- +0x20 / +0x24: the allocation-slack flags that Malloc reads.

### CExoResMan::Demand @0x00711C20 (KNOWN, same as K1)
It is reached through `CRes::Demand` @0x007111E0.

- If `id == -1`, it returns 0.
- If `flags & 4` and `data != NULL`:
  - If `demands == 0` and flag 0x100 is set, it unlinks the CRes from the
    release list and clears 0x100.
  - Then it increments `demands` and returns `data`.
- Otherwise, if no async read is in flight, it switches on `id >> 30`:
  - 0: FUN_00713FB0 (BIF)
  - 1: FUN_00713E80 (RIM)
  - 2: FUN_00713BF0 (ERF)
  - 3: FUN_007133A0 (directory)
- `ServiceFromDirectory` @0x007133A0 returns 0 straight away when
  `table_entry` (+0x14) is NULL. So, as on K1, a replay after an eviction
  is silent, not a crash.

### CResWave (KNOWN)
- **Constructor @0x007228F0.**
  - Ghidra's library matcher names it `CStatusBar::CStatusBar`. That name
    is wrong.
  - The body calls the CRes constructor, stores `CResWave::vftable`, and
    zeroes +0x28, +0x2c, +0x30, +0x34 and +0x38.
  - The object is 0x3C bytes: the helper does `PUSH 0x3c` before
    `CALL operator new`.
- **Vtable** `CResWave::vftable` @0x0099C570. Slots, read from the Steam
  exe's bytes:
  - +0x00: scalar deleting destructor @0x00722950
  - +0x0c: `OnResourceFreed` @0x007229C0. If +0x28 is set, it calls
    `AIL_mem_free_lock(data)` and nulls `data`. It then zeroes
    +0x28..+0x38.
  - +0x10: `OnResourceService` @0x00722A30. **Same slot as K1.**
  - +0x14: `GetData` @0x00722D90. It returns `data + 0x1D6` only when
    +0x28 == 0 and +0x2c == 1. Otherwise it returns `data`.
- **`OnResourceService` branches on the global 0x00A10768.**
  - The global is statically 1 in the exe.
  - Its only two `.text` references (0x0070CB0C and 0x00722A41) are
    `CMP` instructions. Nothing writes it.
- With the global set to 1, `OnResourceService` handles a buffer like
  this:
  - Starts with `"BMU V1.0"`: decompressed with `AIL_decompress_ASI`.
  - Starts with `RIFF` (0x46464952): `AIL_WAV_info` must return 1.
    Format 0x11 is decompressed as IMA ADPCM. PCM is kept as it is.
    Then it sets +0x30 = 1 and returns 1.
  - Anything else: it looks for `RIFF` at +0x1D6 and sets +0x2c if it
    finds it. Otherwise it returns 0.
- **So a plain buffer that starts with `RIFF` is accepted. The fake-header
  quirk is the same as on K1.**

### CResHelper<CResWave,4>::SetResRef @0x0070A8D0 (KNOWN, same as K1)
The facade calls it at 0x0070AF6F with `ECX = internal`. So the helper is
the head of `CExoSoundSourceInternal`.

1. **Early-out:** if `memcmp(new, internal+0x0c, 0x10) == 0`
   (FUN_00710B40), it returns without doing anything.
2. If a CRes is held (+0x08) and the old resref is not `""`:
   - It calls `CancelRequest` (FUN_007111C0) if `requested` (+0x04) is
     set.
   - It calls `ReleaseResObject`.
   - If that returns 0, it deletes the CRes through vtable slot 0 with
     flag 1 and clears +0x08.
3. If the new resref is `""`, it clears +0x08 and copies `""` into
   +0x0c.
4. Otherwise it calls `GetResObject(resref, 4)` @0x00712970.
   - If that returns NULL, it does `operator new(0x3C)`, then the
     CResWave constructor, then `SetResObject(resref, 4, res)`
     @0x00712B60.
   - **SetResObject links the CRes only when FUN_00712360 finds a key
     entry whose `res` is still NULL.** Only in that case does it write
     `table_entry`, set refcount to 1 and copy the id.
   - With no key, the new CResWave keeps id -1, refcount 0 and no table
     entry. It is private to this helper.
5. It stores the CRes at +0x08.
   - If `param_2 != 0`, it sets `requested` and calls `CRes::Request`
     (FUN_007112D0). The mod passes 0.
   - It then copies the resref into +0x0c.

**The facade `CExoSoundSource::SetResRef` @0x0070AF20** does three things
before it calls the helper:
- It calls `CExoSoundSource::Stop` @0x0070AF80.
- It sets `internal+0x60` to 0. That field is the cached duration; on K1
  it is +0x58.
- It calls FUN_00705D70 on `[internal+0x4c]` (the CExoSound object). That
  function is a large sound-manager update loop. Why it is called here is
  open (see below). It does not touch the new source's CRes.

### CExoSoundSourceInternal (0xA8; constructor @0x0070E9F0) (KNOWN)
The size comes from the facade constructor @0x0070AAB0, which calls
`operator new(0xa8)`.

- +0x04 requested, +0x08 `CRes*`, +0x0c CResRef. **Same as K1.**
- +0x1c `do_3d`. **On K1 it is at +0x10.**
  - Play tests `[+0x1c]` before the mono check, at 0x0070F0E4.
  - The range check FUN_0070F880 runs only when +0x1c is set.
- +0x24 looping. Play uses `+0x24 == 0` as the loop count.
- +0x28 one-shot auto-delete.
- +0x2c delete-me.
- +0x30 `resource_demands`. **Same as K1.**
  - `Internal::Demand` @0x0070EC50 stores `CRes::Demand(...) != 0` here.
  - `Internal::Release` @0x0070ECB0 clears it, then calls
    `CRes::Release`.
- +0x38 2D voice, +0x3c 3D voice. **Same as K1.** The voice's +0x04 is the
  Miles HSAMPLE.
- +0x4c `CExoSound*`. This is the constructor argument, `*0x00A1B494`.
- +0x50 base frequency and +0x5c applied rate. Play writes both from the
  WAV's sample rate. This matches the `Pick(0x48, 0x50)` and
  `Pick(0x54, 0x5c)` that `audio_loop.cpp` already uses.
- +0x60 cached duration in ms (Play's return value).
- +0x74 far distance, used by the 3D range check.

### CExoSoundSourceInternal::Play @0x0070ED50 (KNOWN)
The facade `Play` @0x0070AD50 calls it as `Play(0, 0)`.

1. If the sound system is off (`[[+0x4c]+0x0c] == 0`), it returns the
   cached duration.
2. If a 3D or 2D voice already holds a sample, it restarts that sample
   (`AIL_stop_sample`, `AIL_set_sample_position(0)`,
   `AIL_resume_sample`) and returns.
3. If `+0x60 == 0` and `+0x30 == 0`, it reads the WAV:
   - It calls `Internal::Demand`. On 0 it returns.
   - It calls `res->vtable[+0x14]()` (GetData) and passes the result to
     `AIL_WAV_info` (IAT slot 0x0098652C). The info struct is at
     EBP-0x44: rate at EBP-0x38, bits at EBP-0x34, channels at EBP-0x30.
   - It stores the duration at +0x60.
4. **Mono refusal** (0x0070F0E4-0x0070F102): if `[+0x1c] != 0` and
   `channels != 1`, it calls `Internal::Release` and returns the duration
   with no voice. This is the same trap as K1: a non-zero return does not
   prove the sound is playing.
5. It stores the WAV rate at +0x50 and +0x5c.
6. **Range check** (FUN_0070F880): if the source is farther from the
   listener than +0x74, it releases and returns with no voice.
7. It applies the per-group voice limits (FUN_00709010 / FUN_007097D0),
   as on K1.
8. **It attaches the voice synchronously, inside the same call:**
   - It gets a 3D voice with FUN_00708C50(priority group).
   - It demands again if needed.
   - It calls `PlaySourceOn3DVoice` @0x00707CF0.
   - The 2D path is FUN_00708E30 / FUN_00708070.

**`PlaySourceOn3DVoice` @0x00707CF0:**
1. It first writes `internal+0x3c = voice`.
2. It calls `AIL_set_sample_file(voice[1], res->GetData(), 0)`. This
   sample-API call takes **a pointer to an in-memory file image. No file
   handle is involved.**
3. It then sets the loop count, `AIL_set_sample_3D_distances`, the
   position, `AIL_set_sample_3D_position` and the playback rate.
4. It finishes with `AIL_resume_sample`.

So `IsVoiceAttached()` (`[internal+0x3c] != NULL` right after Play) works
unchanged on K2.

Not decompiled: Play also calls FUN_00734010 (a CExoString helper) and
`CRes::GetDemands`, and ignores the result. It looks like leftover debug
code and has no effect.

### Release path for a keyless CRes (KNOWN)
- **`CExoResMan::Release` @0x007130B0** (through `CRes::Release`
  @0x007112B0):
  - If `id >> 30 == 1`, it calls the RIM packed-file vtable. 0xC0000000
    is class 3, so this branch is never taken.
  - When `demands` goes from 1 to 0 and flag 0x100 is clear, it links the
    CRes onto the release list (FUN_0054AA10) and sets 0x100. The data
    stays cached.
- **`CExoResMan::ReleaseResObject` @0x00713010:**
  - If refcount < 2, it sets refcount to 0.
  - If `demands == 0`, it clears `table_entry->res`. **That write is
    null-guarded:** `if ([res+0x14] != 0)`.
  - It then calls `CRes::Dump(1)` (0x00711230, which calls
    `CExoResMan::Dump` @0x00711E60).
  - It adjusts flags 0x8 / 0x200 and returns 0. The helper then deletes
    the CResWave.
- **`CExoResMan::Dump` @0x00711E60:**
  - If `demands == 0`, flags has 4 or 0x10, and `data != NULL`, it calls
    `Free`.
  - If `demands != 0`, it only sets 0x100.
- **`CExoResMan::Free` @0x00711FE0:**
  - It acts only if `data != NULL` and the CRes is on the release list at
    `CExoResMan+0x20`.
  - It calls vtable +0x0c (`OnResourceFreed`).
  - If flag 0x10 is clear, it unlinks the CRes, calls `FreeResourceData`
    @0x007120C0, and clears flags 4, 0x10 and 0x100.
- **`FreeResourceData` @0x007120C0:**
  - If `data != NULL` and `id >> 30 != 1`, it frees `data` with
    FUN_009196FD, which is operator delete. The CResWave scalar deleting
    destructor @0x00722950 uses the same function. When +0x20 is set it
    frees `data - 6` instead.
  - It then credits the budget (+0x08) with `size`.
  - It always zeroes `size` and `data`.
- **Result: ownership is identical to K1.** The engine frees our
  Malloc'd buffer with the matching allocator when the source is
  re-pointed or destroyed. A CRes that was never demanded is never on the
  release list, so its data leaks once, exactly as on K1.

### Differences from K1 (KNOWN)
- **`do_3d` moved** to internal +0x1c (K1: +0x10). The mod sets it
  through the `Set3D` facade, so nothing changes for us.
- **The cached duration moved** to internal +0x60 (K1: +0x58). The facade
  SetResRef clears it, so a fresh injection still goes through Demand.
- **Base frequency and applied rate moved.** `audio_loop.cpp` already
  handles this.
- **Miles API:** the K2 3D path uses the sample API
  (`AIL_set_sample_file` on an HSAMPLE). K1 uses
  `AIL_set_3D_sample_file`. Both take an in-memory WAV image.
- **RIM guard in the free path:** `FreeResourceData` skips the CRT free
  for RIM-class ids (`id >> 30 == 1`). The K1 section does not record
  this guard. It does not matter for 0xC0000000, but it is one more
  reason never to use 0x40000000.
- **Facade SetResRef:** it runs `Stop` and the CExoSound update
  FUN_00705D70 where K1 runs ShutDownSource. This is harmless for
  injection.

### Open (K2)
- Route A has not been tested end to end in game on K2.
- Why the facade SetResRef runs FUN_00705D70 (the sound-manager update)
  on every resref change. Suspected to be a K2 addition that replaces
  ShutDownSource. It is cheap but not free per utterance.
- The initial budget in the K2 `CExoResMan` constructor was not checked.
  FreeChunk eviction is assumed to be as rare as on K1.

### Ready-to-paste K2 column for audio_loop.cpp
```
const size_t   kInternalResourceOffset = acc::off::Same(0x08);  // helper SetResRef 0x0070A8D0 writes [ECX+8]
const size_t   kInternalDemandedOffset = acc::off::Same(0x30);  // Internal::Demand 0x0070EC50
const size_t   kResDemandsOffset       = acc::off::Same(0x04);  // CRes ctor 0x007110D0, Demand 0x00711C20
const size_t   kResIdOffset            = acc::off::Same(0x08);  // ctor sets -1; Demand tests -1, switches on id>>30
const size_t   kResFlagsOffset         = acc::off::Same(0x0c);  // Demand: flags & 4 && data
const size_t   kResDataOffset          = acc::off::Same(0x10);  // Malloc stores the operator new result
const size_t   kResSizeOffset          = acc::off::Same(0x18);  // Malloc reads it, ServiceFromDirectory writes it
const size_t   kResServiceVtableSlot   = 0x10 / 4;              // CResWave::vftable 0x0099C570 +0x10 = 0x00722A30
const uint32_t kResFlagLoaded          = 0x4;                   // unchanged
const uint32_t kInjectedResId          = 0xC0000000u;           // class 3, clear of the RIM (class 1) paths
const uintptr_t kAddrExoResManPtr      = acc::addr::PickGlobal(0x007A39E8, 0x00A1B490);
const uintptr_t kAddrCExoResManMalloc  = acc::addr::Pick(0x004077B0, 0x00712F30);  // __thiscall(resman, CRes*), RET 4, returns 1/0
```
These values already in `audio_loop.cpp` stay valid on K2:
- `kSoundSourceInternalOffset Same(0x04)`
- `kInternalVoice3DOffset Same(0x3c)`. `PlaySourceOn3DVoice` writes
  `[internal+0x3c] = voice`.
- `kVoiceHandleOffset Same(0x04)`. Witness:
  `AIL_set_sample_file(voice[1], ...)`.
- `kInternalBaseFrequencyOffset Pick(0x48, 0x50)`. Play writes the WAV
  rate there.
