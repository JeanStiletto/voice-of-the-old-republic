# manifest.toml (26 lines)

Patch metadata consumed by KPatchCore's PatchApplicator. `[patch]` declares
id/name/version/author/description plus empty `requires`/`conflicts` lists.
`[patch.supported_versions]` maps named build identifiers
(`kotor1_steam_103`, `kotor1_gog_103`, `kotor1_gog_103_cdrepack`,
`kotor1_allard_ru_172`) to SHA-256 hashes of the target exe — this is the
hard-gate KPatchCore checks before applying (see memory
project_kpatchcore_hash_hardgate). The three non-Allard builds share
byte-identical engine code at every hook offset (project
_ghidra_gog_steam_bytes_match) and are covered by the same `hooks.toml`; the
Allard Russian 1.72 build is the same source relinked with per-function
offset drift, so its hooks come from a separate `allard.hooks.toml` with
runtime rebasing via `acc::addr::R` keyed off the PE link timestamp.

Current version: 0.6.3.
