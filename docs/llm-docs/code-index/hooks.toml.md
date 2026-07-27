# hooks.toml (1214 lines)

KPatchManager hook declarations for the vanilla 1.0.3 layout (Steam, GoG,
and one GoG-equivalent CD re-pack — byte-identical at every hook offset per
project_ghidra_gog_steam_bytes_match). `[metadata].target_versions` pins
this file to those three SHA-256 hashes only; the Allard Russian 1.72 build
uses a separate `allard.hooks.toml` with per-function-relocated addresses.
Each `[[hooks]]` entry documents, in a long comment block, the exact
disassembly at the hook site, why that cut point was chosen (register vs.
stack-param sourcing — `esp+X` sources emit LEA not MOV, a known upstream
bug, so hooks prefer register sources per feedback_hook_design_register_
sources), and — for consuming hooks — the `consumed_exit_address` and which
register is excluded from POPAD restore so a handler's int return survives
into the wrapper's `TEST EAX,EAX` dispatch. A large tombstoned block
(L564-586) documents four CSWGuiListBox entry-point hooks that were tried
and reverted (they caused title-screen focus oscillation) — the lesson
(entry-point hooks on CSWGuiListBox are unsafe; redesign as mid-function
register-sourced hooks) is preserved as a warning against re-attempting it
the same way. One commented-out hook (L784-810, `OnAddMessages`) documents
a disabled path superseded by `OnAppendToMsgBuffer`.

## Hook groups by target subsystem

**Engine bring-up / rules** — `OnRulesInit` (CSWRules::CSWRules ctor,
detour, non-consuming, address borrowed from upstream LevelUpLimit).

**GUI focus & panel navigation** (CSWGuiControl/CSWGuiPanel/CSWGuiListBox,
5 hooks, all mid-function register-sourced detours): `OnHandleFocusChange`,
`OnSetActiveControl` (the canonical once-per-change focus signal used for
screen-reader announcements), `OnListBoxSetActiveControl` (listbox row
focus doesn't bubble to panel SetActiveControl, so it needs its own hook),
`OnHandleInputEvent` (CSWGuiManager's central input dispatcher — every
GUI-routed key/mouse event on every screen; consuming — swallows arrow keys
it's already handled so the engine's broken .gui focus cycle doesn't
double-fire), `OnUpdate` (per-frame manager tick, used as a deferred-call
site so chain-nav mouse moves don't re-enter mid-dispatch).

**In-world / client-app input pipeline** (3 detours): `OnProcessInput`
(frame-boundary seq marker), `OnClientHandleInputEvent` (upstream
client-app HandleInputEvent — in-world hotkeys, Esc-from-world, target
actions; consuming — swallows Esc while a mod overlay is active so
ShowSWInGameGui(7) doesn't pop Options on top of it), `OnSetSWGuiStatus`
(diagnostic-only, traces sw_gui_status transitions).

**Sub-screen lifecycle** (2 detours): `OnSwitchToSWInGameGui` (pops any
already-open sub-screen via PrevSWInGameGui before the new one opens, fixing
InGameOptions' undead-panel-stack accumulation), `OnHideSWInGameGui`
(diagnostic — identifies the caller path for pause auto-close).

**Combat log** (1 detour): `OnAppendToMsgBuffer` (CGuiInGame's live
combat-log ring-buffer append — the confirmed real source of combat-log
rows; supersedes the disabled/commented `OnAddMessages`, which only fires
on the dialog path).

**Audio** (2 detours): `OnSetListenerPosition` (CExoSound — lets
view_mode's OnUpdate-time listener writes win the per-frame race against
the engine's camera-driven write; skip_original_bytes + consumed-exit so
the wrapper's TEST EAX,EAX can't clobber engine EFLAGS/EAX — documents two
prior dead-end attempts at PlayFootstep in detail), `OnCalculatePitch
VarianceFrequency` (neutralises the engine's per-fire random pitch jitter,
but ONLY for sounds fired inside our own PlayCue/PlayCue3D scope — every
other engine sound keeps its jitter), `OnPlayFootstep` (stuck-detection
footstep suppression — same wrapper-EFLAGS/EAX pitfall documented at length
as the canonical example of the bug class).

**Object targeting** (1 detour): `OnShowObject`
(CClientExoAppInternal::ShowObject — the engine's single point of
user-driven main-interface target change, distinct from AI churn on
last_target; currently diagnostic-only).

**Combat-queue diagnostic probes** (4 read-only detours, all logging into
"Combat.Diag"): `OnCombatRoundAddAction`, `OnCombatRoundRemoveAllActions`,
`OnCombatRoundSetCurrentAction`, `OnCombatRoundRemoveLastAction` — answer
"did the queue chain or overwrite" without racing the engine; safe to leave
in indefinitely.

**Pause state** (1 detour): `OnSetPauseState`
(CServerExoAppInternal::SetPauseState — every pause mutation, mask-based
not bit-index; polling the state field directly was a dead end).

**Turret minigame** (2 detours): `OnTurretBulletHit` (CSWMiniGameObject::
OnHitBullet — confirmed-hit diagnostic for the aim-assist hit-radius
question), `OnPlayerFire` (CSWMiniPlayer::Fire — player-only shot counter
for the aim-assist accuracy A/B).

**Doors** (1 detour): `OnDoorOpen` (CSWSDoor::OpenDoor, mid-function at the
opener-id stamp — drives the post-open camera-facing re-orientation
announce in door_announce.cpp).
