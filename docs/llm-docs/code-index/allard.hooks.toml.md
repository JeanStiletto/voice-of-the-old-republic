# allard.hooks.toml (443 lines)

Generated hooks file for the Allard Russian build of swkotor.exe. Mirrors `hooks.toml`'s hook set 1:1 — same `original_bytes` at every site since the engine code is byte-identical between builds, only relocated (+16..+512 bytes per function, produced by the linker). Addresses come from `kdev sigscan` (relocation-tolerant signature match); `consumed_exit_address` values are NOT sigscan output — they were manually rebased by each hook's own delta and byte-verified against the reference image (2026-07-25). Design rationale for each hook intentionally lives only in `hooks.toml`, not duplicated here, so this file stays mechanical/regeneratable.

## Declarations (in source order)

- L19 — `[metadata] target_versions = [...]` — single SHA-256 for the Allard build
- L22 — `[[hooks]] OnRulesInit` — detour @0x00552dba (was +288B); param esi:pointer
- L35 — `[[hooks]] OnHandleFocusChange` — detour @0x004189cb (was +96B); params edi:pointer, eax:int
- L52 — `[[hooks]] OnSetActiveControl` — detour @0x0040a6d8 (was +160B); params edi:pointer, esi:pointer
- L69 — `[[hooks]] OnHandleInputEvent` — detour @0x0040c9f7 (was +240B); exclude_from_restore=["eax"], consumed_exit_address @0x0040ccbb; params ecx/ebx/eax
- L92 — `[[hooks]] OnUpdate` — detour @0x0040cf66 (was +240B); param ebp:pointer
- L105 — `[[hooks]] OnSetMoveToModuleString` — detour @0x004aed30 (was +96B); params ecx:pointer, esp+4:pointer
- L122 — `[[hooks]] OnListBoxSetActiveControl` — detour @0x0041c1cb (was +96B); params esi/ecx/eax
- L143 — `[[hooks]] OnPlayFootstep` — detour @0x0061a4ba (was +416B); skip_original_bytes=true, exclude_from_restore=["eax"], consumed_exit_address @0x0061a7d2; param esi:pointer
  note: mid-function cut replacing engine's JZ — see audio_footstep_suppress.cpp for why
- L158 — `[[hooks]] OnSetListenerPosition` — detour @0x005d5ff0 (was +512B); skip_original_bytes=true, exclude_from_restore=["eax"], consumed_exit_address @0x005d5ffb; params ecx/esp+4
- L177 — `[[hooks]] OnCalculatePitchVarianceFrequency` — detour @0x005db580 (was +432B); exclude_from_restore=["eax"], consumed_exit_address @0x005db5f9; param ecx:pointer
- L192 — `[[hooks]] OnSwitchToSWInGameGui` — detour @0x0062d0bd (was +400B); params ecx:pointer, ebx:int
- L209 — `[[hooks]] OnProcessInput` — detour @0x0062298b (was +400B); param ecx:pointer
- L222 — `[[hooks]] OnClientHandleInputEvent` — detour @0x006213a0 (was +400B); exclude_from_restore=["eax"], consumed_exit_address @0x006222b0; params ecx/esp+4/esp+8
- L245 — `[[hooks]] OnSetSWGuiStatus` — detour @0x0062ab90 (was +400B); params ecx/esp+4/esp+8
- L266 — `[[hooks]] OnAppendToMsgBuffer` — detour @0x0062b750 (was +400B); params ecx/esp+4
- L283 — `[[hooks]] OnHideSWInGameGui` — detour @0x0062cd30 (was +400B); params ecx/esp+4
- L300 — `[[hooks]] OnShowObject` — detour @0x005f9e8e (was +512B); params ebx:pointer, eax:int
- L317 — `[[hooks]] OnCombatRoundAddAction` — detour @0x004d3770 (was +272B); params ecx/esp+4/esp+8
- L338 — `[[hooks]] OnCombatRoundRemoveAllActions` — detour @0x004d3880 (was +272B); param ecx:pointer
- L351 — `[[hooks]] OnCombatRoundSetCurrentAction` — detour @0x004d2c50 (was +272B); params ecx/esp+4
- L368 — `[[hooks]] OnCombatRoundRemoveLastAction` — detour @0x004d38c0 (was +272B); param ecx:pointer
- L381 — `[[hooks]] OnSetPauseState` — detour @0x004b8230 (was +288B); params ecx/esp+4/esp+8
- L402 — `[[hooks]] OnTurretBulletHit` — detour @0x0066c1a0 (was +16B); param ecx:pointer
- L415 — `[[hooks]] OnPlayerFire` — detour @0x0066dc60 (was +16B); param ecx:pointer
- L428 — `[[hooks]] OnDoorOpen` — detour @0x00589deb (was +256B); params esi:pointer, edi:int
