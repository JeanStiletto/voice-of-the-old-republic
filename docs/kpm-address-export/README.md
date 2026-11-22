# Address data for Kotor Patch Manager

Engine data mapped while building Voice of the Old Republic, exported into
KPM's AddressDatabase schema. It contains names, addresses and offsets only, with no decompiled code.
Function names are KPM's own KOTOR 1 names, so a lookup by
(class_name, function_name) resolves in both games.

Rebuilt 2026-11-22 on KPM master (commit fd639f1c). Every database is KPM's
current database with rows added; existing KPM rows are never changed.

## Databases

**kotor2_gog_aspyr.db**
- classes: vtable filled from the GOG exe's RTTI for 330 classes (the table was empty). size is left NULL. Template instantiations are omitted.
- functions: 7 added. KPM already had every other function we know.
- 6 of our tested rows disagree with KPM; KPM's rows were kept. See "Rows where KPM's GOG db looks wrong" below.

**kotor1_relink_2004.db**
- The 2004-03-05 relink of KOTOR 1 1.0.3, shipped as Allard Russian 1.72 and as LEM Polish 1.03. Both hashes are in game_version.
- It is the same compile, relinked: code is byte-identical but functions moved. .data did not move.
- offsets, classes and global_pointers are copied from current kotor1_0_3.db. All 44 global pointers lie in .data, so they hold on this build unchanged.
- functions: the 212 we relocated (by signature scan, cross-checked against hook bytes).
- classes.vtable: the 41 we relocated; the others are NULL.

**kotor2_steam_aspyr.db**
- Kept for reference; KPM pull request 370 supersedes it.
- 726 functions added: 165 the mod calls or hooks (notes `Voice of the Old Republic: used by the mod on this build`, tested in game) and 561 vtable-slot leads.

## Changes since the first export

- **Slot 0 is the scalar deleting destructor.** KPM's KOTOR 1 db names that function Destructor / Destructor_2; the K1 name is no longer carried across. 185 rows are now named `scalar_deleting_destructor`.
- **Shifted vtable pairings withdrawn.** Equal slot counts do not guarantee aligned slots: KOTOR 2 inserted virtuals in mid-table (CSWGuiPanel gained one ahead of the message-box slots) and dropped others. Such a shift shows as a run of rows each named after its neighbour. Every class with any row that disagrees with KPM's GOG names is withdrawn: 390 rows in 36 classes, listed in `k2-vtable-slot-rows-withdrawn.csv`. Of the slot leads KPM's GOG db can check, 858 agreed and 41 did not before this filter.
- **CSWGuiMessageBox::SetMessage (strref) corrected** to K2 slot 28: Steam 0x0075BDE0, GOG 0x0052D370 (KPM's `SetMessage_2`). The old 0x0040FFB0 was slot 27, a different function.

## Rows where KPM's GOG db looks wrong

Addresses are GOG. Each disputed function is byte-identical in Steam and GOG.
- `CSWSCreature::GetBlind` 0x0069DF00: a tiny accessor testing bits 0x10, 8 and 2 of the byte at +0xff4, the same logic as K1's GetBlind. KPM's GetBlind at 0x0068F7D0 is a large function.
- `CSWSModule::GetModuleResourceName` 0x007336C0: copies the module's string at +0x5c into its return value. KPM names it `CSWCMessage::ReadCExoLocStringClient_2`.
- `CSWCArea::AddMapPin` 0x00456040: the engine's script pin creator allocates a CSWCMapPin (0x110 bytes, its ctor) and passes it here. KPM names it `AddWeatherObject`.
- `CSWGuiPazaakGame::HandleContinue` 0x005558A0 / `HandleStand` 0x005559B0: our names follow K1's and are tested in game (End Turn and Stand). KPM has them swapped.

These are naming differences only:
- `CExoSoundInternal::SetSoundMode` / `CExoInputInternal::PollInput`: on K2 these addresses are the facade classes, CExoSound and CExoInput, as KPM has them.
- `CExoString::operator=` 0x006059E0: KPM's `operator=_2` (another overload number).
- `CExoSoundSource::Destructor` 0x00639B00: the scalar deleting destructor, as KPM has it.
- `CSWGuiInGameAbilities::DisplayPowers` 0x00595370 is KPM's `IsCharacterJedi`, and `CSWGuiAbilitiesCharGen::GetAbilityPointCost` 0x00602440 is KPM's `DetermineCostToIncreaseScore`. KPM's names describe them well.

## CSV files

- **k2-gog-to-steam.csv**: every function in KPM's current kotor2_gog_aspyr.db mapped to Steam by `kdev sigscan --addresses`. Columns: gog_address, steam_address, method, kpm_gog_names. Method is one of:
  - `signature`: 10,060 rows. A relocation-masked signature is unique in both builds.
  - `call-site vote`: 85 rows. The callers were located in Steam by their preceding bytes and voted, with at least two votes and a 90% majority.
  - `ordinal (inference)`: 137 rows. Byte-identical duplicates matched by address order. Treat as unverified.
  - `ambiguous` (22) or `not-found` (101): 123 rows with no Steam address.
  - 10,405 rows: KPM's 10,426 functions share 10,405 distinct addresses; the last column lists every name at an address.
  - Checks: it agrees with all 185 Steam/GOG pairs the mod uses, and with 49 of the 50 functions in KPM's Steam db. The exception is `CSWSCreatureStats::AddFeat`: KPM's GOG and Steam dbs point at different functions. GOG 0x006F4700 is code-identical to Steam 0x006B8280, not to KPM's Steam 0x006F5B40.
- **k2-vtable-slot-rows-withdrawn.csv**: the withdrawn slot rows, with KPM's GOG name at each address.
- **k2-functions-added.csv**: the added functions, Steam and GOG side by side.
- **k2-data-addresses.csv**: vtables and globals the mod uses (K1 / Steam / GOG), under our constant names.
- **k1-k2-vtable-slots.csv**: the full K1 to K2 slot pairing, 7,000 rows, unfiltered. Use the withdrawn list before relying on it.
- **k2-steam-vtables.csv / k2-gog-vtables.csv**: every RTTI vtable in each exe.
- **k1-k2-offsets.csv**: 304 struct offsets from the mod, giving the K1 value, the K2 value, and whether the two games differ, are the same, or K2 is unknown. Names are ours; class_hint and context come from our source comments, so mapping them to KPM member names needs a human pass.

## Provenance

Generated by `kpm_export.py` (included) from the mod's address tables, our
signature-scan reports and RTTI scans of the exes. Hashes:
- K2 Steam 6A522E71...BFFEF
- K2 GOG 777BEE23...3A914
- Allard 7B961A14...D7628
- LEM F96AC62B...D8137
