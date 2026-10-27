# In-memory sound resources (KOTOR 1) — can the engine play a WAV that has no file?

Research for "positional speech": render TTS to PCM in memory (Prism
`speak_to_memory`), then play it through the engine's own 3D sound path at a
world position, so it gets the engine's panning, distance falloff, volume
sliders and EAX/dsoal reverb, just like the existing positional cues.

Decompiled 2026-10-27 from Lane's GOG database (bytes match Steam for K1).
KOTOR 1 only. Format: known (seen in a decompile), suspected (inferred),
open (not yet checked).

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

- The end-to-end Route A test has not been run (see the proof of concept
  below).
- Whether this Miles build accepts WAVE_FORMAT_IEEE_FLOAT.
- The exact meaning of CRes flags 0x8 and 0x200.
- Whether the vtable +0x10 `OnResourceService` call needs anything besides
  `data` and `size` (it reads only those in the decompile).
- K2 equivalents: not researched.

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
