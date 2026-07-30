# Phase 3 scan — installer dialogs and bundled-mod installers

Scope: `installer/KotorAccessibilityInstaller/` —
WelcomeForm.cs (226), ModSelectionForm.cs (244), ModSelection.cs (38),
GameVersionSelectionForm.cs (177), GameVersionSelection.cs (18),
Kotor2ModSelectionForm.cs (217), Kotor2ModsInstallForm.cs (154),
TslrcmInstallForm.cs (376), WorkshopTlkHarvestForm.cs (290),
UninstallForm.cs (151), ModdingInfoForm.cs (150), InstalledOptionsForm.cs (96),
UpdateAvailableForm.cs (84), CancelConfirm.cs (34),
ModInstallers/K1cpInstaller.cs (293), ModInstallers/TweakPackInstaller.cs (211),
ModInstallers/HoloPatcherRunner.cs (163), ModInstallers/ModInstallerCoordinator.cs (126),
ModInstallers/K2cpInstaller.cs (120), ModInstallers/HoloPatcherProvider.cs (117),
ModInstallers/GitHubTslpatchdataFetcher.cs (82), ModInstallers/ModInstallContext.cs (31),
ModInstallers/IModInstaller.cs (31), ModInstallers/ModInstallResult.cs (30),
PriorityGroup2da.cs (266), SwkotorIniTweaker.cs (259), SpatialAudioManager.cs (249),
IntroMovieDisabler.cs (242), TslrcmDetector.cs (61). 4536 lines total, all read in full.

Method: full read of every file in the batch (no file was skimmed or excerpted).
Cross-file greps run to verify duplication and consistency claims before writing
them up:
- `grep -n "AcceptButton\|CancelButton" *.cs` — confirmed which forms set the
  two properties and which don't.
- `grep -rn "RaiseAutomationNotification" *.cs ModInstallers/*.cs` — found every
  status-update site that does the UIA-notification workaround, to check
  `UninstallForm.UpdateStatus` against the pattern used everywhere else.
- `grep -n "Nullable" *.csproj` and `grep -rn "string?" . --include=*.cs` —
  confirmed the project has `<Nullable>disable</Nullable>` and that
  `PriorityGroup2da.cs:222` is the only nullable-annotated declaration in the
  whole project.
- `grep -n "public async Task<ModInstallResult> InstallAsync\|stagingRoot = Path.Combine\|finally\|Directory.Delete(stagingRoot" K1cpInstaller.cs K2cpInstaller.cs` —
  located the matching staging/cleanup blocks to cite exact line ranges.
- Read HoloPatcherRunner.cs a second time at the constant declarations to
  verify the "just under the forward throttle" comment against the actual
  values (2500 vs 5000).

No file in this batch references `hooks.toml`, `engine_offsets.h`, or any of
the other do-not-touch items — none of the standing traps in the brief apply
to C#/WinForms code, and this batch has no hook addresses, calling-convention
typedefs, or exported symbol names to protect.

## Section A — general low-level cleanup

Accessibility is the primary lens for this batch per the task brief, since it
is almost entirely player-facing dialogs and this is the user's first contact
with the mod. Accessibility defects are reported here in Section A as real
defects, not polish, per the task instructions.

### A1 — UninstallForm.UpdateStatus never announces to the screen reader (UninstallForm.cs:144-149)

What's there now:
```csharp
private void UpdateStatus(string message)
{
    if (InvokeRequired) { Invoke(new Action(() => UpdateStatus(message))); return; }
    _statusLabel.Text = message;
    Logger.Info(message);
}
```
Every other progress-reporting form in this same batch — Kotor2ModsInstallForm.cs:133-152,
TslrcmInstallForm.cs:345-367, WorkshopTlkHarvestForm.cs:267-286 — follows the
`_statusLabel.Text = message` write with a
`_statusLabel.AccessibilityObject?.RaiseAutomationNotification(...)` call,
each with a comment explicitly noting that WinForms Labels are not UIA live
regions and NVDA/JAWS will not speak a changed `Label.Text` on their own. This
is a documented, previously-hit bug pattern in this codebase (see project
memory `feedback_never_silence_fallback_announcement` and
`project_winforms_live_region_uia_notification`). UninstallForm's own
`UpdateStatus` is missing the notification entirely.

Why it's a problem: `UninstallForm.UninstallButton_Click` calls
`UpdateStatus(InstallerLocale.Get("Uninstall_StatusRemoving"))` right before a
potentially multi-second `Task.Run(() => UninstallFlow.PerformUninstall(...))`,
then `UpdateStatus(InstallerLocale.Get("Uninstall_StatusComplete"))` after. A
blind user pressing the Uninstall button hears nothing between the button
click and the final "Uninstallation completed successfully" MessageBox — the
"Removing..." status change is silent, and if the uninstall stalls there is no
spoken cue that anything is happening at all.

Proposed change: add the same `RaiseAutomationNotification` block (with its
`try`/`catch Logger.Warning` guard) that the other three forms already use.
`System.Windows.Forms.Automation` needs adding to the file's usings.

Risk: `low` (additive, no behavior change to the non-accessibility path).
Needs an in-game — well, in-installer — check: run Uninstall with NVDA/JAWS
running and confirm "Removing update..." / completion are both spoken.
Estimated line delta: +10.

### A2 — WelcomeForm never sets AcceptButton or CancelButton (WelcomeForm.cs:43-171)

What's there now: `InitializeComponents` builds two pages (`_page1`,
`_page2`) each with a primary button (`_nextButton`, `_installButton`) and
`_page2` also has `_backButton`. Nowhere in the class is `AcceptButton` or
`CancelButton` assigned. Every sibling wizard-step dialog in this same batch —
ModSelectionForm.cs:189-190, GameVersionSelectionForm.cs:149-150,
Kotor2ModSelectionForm.cs:185-186, ModdingInfoForm.cs:95-96 — sets both.

Why it's a problem: on the very first screen the user sees, Enter does not
activate Next/Install and Escape does not trigger the back button or the
cancel-confirmation flow that `FormClosing` implements
(`CancelConfirm.ConfirmCancel`, WelcomeForm.cs:161-170) — Escape does nothing
at all, because `CancelButton` being unset means the key is never translated
into a `Close()` call. A keyboard-only user has to Tab to the button and press
Space/Enter on it directly, which is exactly the inconsistency the rest of
the wizard avoids.

Proposed change: since `_page1`/`_page2` swap visibility rather than being
separate forms, `AcceptButton`/`CancelButton` need to be kept in sync with
`ShowPage1()`/`ShowPage2()` (there is already a place for this — both methods
already call `.Focus()` on the page's primary button). Set
`AcceptButton = _nextButton` at the end of `ShowPage1()` and
`AcceptButton = _installButton; CancelButton = _backButton;` at the end of
`ShowPage2()`. Page 1 has no back/cancel button, so `CancelButton` should stay
unset there (or point at a to-be-added close affordance — a user decision,
not proposed here).

Risk: `low`. Needs an in-installer keyboard check: Enter on page 1 advances,
Escape on page 2 goes back, Enter on page 2 starts the install.
Estimated line delta: +4.

### A3 — UninstallForm never sets AcceptButton or CancelButton (UninstallForm.cs:25-90)

What's there now: `_uninstallButton` and `_cancelButton` are both created and
added to `Controls`, but neither `AcceptButton` nor `CancelButton` is set.
Escape therefore does not close the dialog, unlike every wizard-step form in
this batch (see A2's list). Enter's absence here is arguably safer (it stops
Enter from accidentally starting an uninstall), but the missing
`CancelButton` removes the one navigation shortcut that costs nothing to
offer.

Proposed change: `CancelButton = _cancelButton;` after the
`Controls.AddRange` call. Do not set `AcceptButton` — an accidental Enter
press starting a destructive uninstall is a real risk this form's current
button-only design already avoids (there's already a confirmation
`MessageBox` in `UninstallButton_Click`, but the extra friction of requiring
a deliberate button click is reasonable to keep for the primary action).

Risk: `mechanical`. Estimated line delta: +1.

### A4 — InstalledOptionsForm and UpdateAvailableForm never set AcceptButton or CancelButton (InstalledOptionsForm.cs:21-94, UpdateAvailableForm.cs:15-82)

What's there now: both forms lay out four buttons in a row (reinstall/toggle
audio/collect logs/close in InstalledOptionsForm; update/full
install/toggle audio/close in UpdateAvailableForm) and neither sets
`CancelButton`. `UserChoice` already defaults to `UpdateChoice.Close` in both
classes, so the "do nothing, just leave" outcome already exists and has a
button (`closeButton`) — it's just not wired to Escape.

Why it's a problem: these are the first screens returning users see (mod
already installed). A keyboard user who wants to back out has to Tab through
up to four buttons to reach Close; Escape — the universal "get me out of
here" key every other dialog in the app honors — does nothing.

Proposed change: `CancelButton = closeButton;` in both forms, right after
`Controls.AddRange`. No `AcceptButton` — four meaningfully different actions
means there is no safe default to arm on Enter.

Risk: `mechanical`. Estimated line delta: +1 per file (+2 total).

### A5 — ProgressBar controls have no AccessibleName (Kotor2ModsInstallForm.cs:76-82, TslrcmInstallForm.cs:126-132, WorkshopTlkHarvestForm.cs:117-122, UninstallForm.cs:58-64)

What's there now: all four `ProgressBar` instances in this batch are created
without an `AccessibleName`. `ProgressBar.TabStop` defaults to `false`, so
this is not a keyboard-reachability gap — a Tab-only user will never land
focus on it — and the percentage/status is separately spoken through each
form's `RaiseAutomationNotification` status updates (already covering the
content that matters). This is lower priority than A1-A4: flagging it because
the brief calls out "controls with no AccessibleName" explicitly, but it is
genuinely minor here since the bar is not reachable and its information is
already announced by other means (except in UninstallForm, which is A1).

Proposed change: give each a short `AccessibleName` (e.g. "Uninstall
progress") for the case where a screen reader's object-navigation mode (not
just Tab) is used to inspect the dialog.

Risk: `mechanical`. Estimated line delta: +1 per file (+4 total).

### A6 — Sole nullable-annotated declaration in a `<Nullable>disable</Nullable>` project (PriorityGroup2da.cs:222)

What's there now:
```csharp
row.TryGetValue(c, out string? val);
val ??= "";  // unknown column -> empty cell (defensive, forward-compat)
```
`grep -rn "string?" . --include=*.cs` (excluding bin/obj) returns exactly this
one line in the entire project. The `.csproj` has `<Nullable>disable</Nullable>`
(confirmed via `grep -n "Nullable" *.csproj`), so the `?` annotation is inert —
it compiles but is not checked, and nowhere else in ~4500 lines of this batch
(or apparently the rest of the installer) uses the nullable-reference-type
syntax.

Why it's a problem: inconsistent with the project's own nullability setting;
a reader who sees `string?` here and nowhere else may reasonably wonder if
nullable checking is enabled for just this file, which it is not.

Proposed change: drop the `?` — `out string val`. The `??=` line already
handles a null return from `TryGetValue` defensively regardless of the
annotation.

Risk: `mechanical`. Estimated line delta: 0 (one-character removal).

### A7 — Stale comment contradicts its own constant (ModInstallers/HoloPatcherRunner.cs:23-26)

What's there now:
```csharp
// Heartbeat tick when HoloPatcher hasn't said anything forwardable.
// Set just under the forward throttle so an "any progress?" update lands
// at predictable intervals even when HoloPatcher goes quiet.
private const int HeartbeatMs = 5000;
```
`ForwardThrottleMs` (line 21, same file) is `2500`. `HeartbeatMs` at `5000` is
double `ForwardThrottleMs`, not "just under" it — there is no "just under"
relationship between the two values at all.

Why it's a problem: the comment describes a design relationship between the
two constants that the actual numbers don't have. Read literally it would
lead a future editor to believe changing `ForwardThrottleMs` should keep
`HeartbeatMs` slightly below it, which is not how the code actually behaves —
the heartbeat loop (lines 99-117) fires unconditionally every `HeartbeatMs`
and independently checks `now - lastForwardTicks < HeartbeatMs - 500` to
decide whether to stay quiet. The two constants are independent tuning knobs,
not a "just under" pair.

Proposed change: rewrite the comment to describe what the code actually does,
e.g. "Independent poll interval; the loop skips emitting when a stdout
forward landed within the last `HeartbeatMs - 500` ms, so this only speaks
when HoloPatcher has genuinely gone quiet." No code change.

Risk: `mechanical` (comment-only). Estimated line delta: 0.

## Section B — AI-pattern findings

### B1 — The UIA-notification "speak this label update" block is copy-pasted four times (Kotor2ModsInstallForm.cs:133-152, TslrcmInstallForm.cs:345-367, WorkshopTlkHarvestForm.cs:267-286, plus MainForm.cs — out of this batch's scope, confirmed present via `grep -rn "RaiseAutomationNotification"`)

What's there now: three files in this batch each define a private
`UpdateStatus` method that does: marshal to the UI thread if needed, set
`_statusLabel.Text`, then `try { _statusLabel.AccessibilityObject?.RaiseAutomationNotification(AutomationNotificationKind.ActionCompleted, AutomationNotificationProcessing.MostRecent, message); } catch (Exception ex) { Logger.Warning(...); }`.
TslrcmInstallForm.cs's own comment literally says "Same live-region
workaround as MainForm.UpdateStatus", confirming the author knew it was a
repeat of an existing pattern each time it was added. `Kotor2ModsInstallForm.cs`'s
comment says the same thing.

Why it's a problem: this is the exact "copy-paste block an abstraction
should own" pattern the AI-pattern sweep is meant to catch — four
near-identical ~15-20 line methods, differing only in whether marshaling uses
`Invoke` vs `BeginInvoke`, and whether the caller can suppress the
announcement (`announce` bool parameter present in two, absent in the
other two). It's also the exact class of gap that produced A1: because the
notification logic isn't a single shared call, `UninstallForm.UpdateStatus`
was written without it and nothing caught the omission at compile time.

Proposed change: extract a small shared helper, e.g. a static
`AccessibleStatus.Announce(Control owner, Label label, string message, bool announce = true)`
that does the marshal-if-needed + text-set + notification-with-catch in one
place. Each of the four call sites collapses to one line; a future form gets
the correct behavior by construction instead of by remembering to copy the
block.

Risk: `low` (mechanical extraction of already-identical logic; behavior
should be unchanged for the three existing correct call sites, and A1's fix
folds into this same helper instead of being a fourth divergent copy).
Estimated line delta: roughly -45 across the three call sites, +20 for the
shared helper — net negative, and it fixes A1 as a side effect if done in the
same pass.

### B2 — K1cpInstaller and K2cpInstaller share a near-identical InstallAsync skeleton (K1cpInstaller.cs:39-124, K2cpInstaller.cs:46-118)

What's there now: both `InstallAsync` methods, in order: check
`ctx.HoloPatcherExePath` exists and fail with the same-shaped message if not;
build a `stagingRoot` under `Path.GetTempPath()` with a mod-specific GUID
prefix; create `tslpatchdata` under it; call `ctx.StatusUpdate?.Invoke` +
`ctx.Progress?.Invoke(0)`; call `GitHubTslpatchdataFetcher.FetchAsync` with a
progress remap to `0..55`; more status/progress calls; call
`HoloPatcherRunner.RunAsync` with mod-specific locale-key strings; return
`ModInstallResult.Fail`/`Ok`; and a `finally` block that deletes
`stagingRoot` with an identical try/catch/Logger.Warning shape
(K1cpInstaller.cs:113-123 and K2cpInstaller.cs:107-117 are structurally
identical, differing only in the log message's mod name). K1CP additionally
does a locale-overlay step and a line-ending normalization pass that K2CP
does not — genuinely mod-specific behavior, not something a shared skeleton
should hide.

Why it's a problem: this is duplication across the two concrete
`IModInstaller` implementations that the task brief explicitly said would be
a fair finding to report (the coordinator + interface are shared already;
the concrete installers are not). A third TSLPatcher-driven installer (any
of the `TODO` list in `ModInstallerCoordinator.BuildPipeline`) would very
likely copy this skeleton a third time.

Proposed change: not prescribing an exact shape (that's a design decision for
the user), but the natural extraction is a shared protected/internal helper —
e.g. `TslpatchdataModInstallerBase` or a static
`GitHubTslpatchdataModInstaller.RunAsync(ctx, owner, repo, pinnedRef, displayId, ..., Action<string> applyOverlay = null)`
— that both installers call, with K1CP's overlay/normalization passed in as
the mod-specific extension points.

Risk: `low` (both call sites are simple enough to diff mechanically against
the extracted version; needs a dry run of a K1CP install afterward since it's
the only one of the pair actually wired into a pipeline today —
`ModInstallerCoordinator.BuildPipeline()` only includes `K1cpInstaller`).
Estimated line delta: roughly -60 net once a third TSLPatcher installer is
added; roughly neutral with just these two.

### B3 — IntroMovieDisabler.DisableIntros and RestoreIntros are a hand-mirrored pair (IntroMovieDisabler.cs:63-148, IntroMovieDisabler.cs:156-240)

What's there now: `DisableIntros` renames `<name>.bik` to
`<name>.bik.disabled`; `RestoreIntros` renames it back. Both methods walk the
same `IntroFiles` array, both handle the same three cases (already in target
state + stray other-state file present -> delete the stray; already in
target state alone -> count `AlreadyDone`; neither file present -> count
`Missing`; the rename itself), both accumulate `renamed`/`alreadyDone`/`missing`/`errors`
and build an identical `Result` shape at the end. The two methods differ only
in which of `src`/`dst` is the "already there" name and which is the "stray to
delete" name.

Why it's a problem: it's a same-file, ~85-line mirror duplication — the
brief's "small repetitions inside a file" bucket, but large enough (and
symmetric enough) to be worth naming explicitly. A bug fixed in one direction
(e.g. the stray-file cleanup's try/catch) is easy to forget in the other.

Proposed change: extract a private
`RenameSet(string moviesDir, bool disable)` (or an inner
`SwapNames(string from, string to)` helper called once per file with the
two name orders swapped by the caller) that both public methods call, passing
the direction. Given the doc comments already describe the two as inverses of
each other, this is a low-risk collapse.

Risk: `low` (behavior-preserving; the two call sites already agree on every
case). Estimated line delta: roughly -60.

### B4 — SpatialAudioManager.SetEaxValue reimplements SwkotorIniTweaker's section-patch algorithm (SpatialAudioManager.cs:145-225, compare SwkotorIniTweaker.cs:113-236)

What's there now: `SwkotorIniTweaker.ApplySectionPairs` is a general
"find-or-append a `[Section]`, then find-or-append each `key=value` pair
inside it, preserving ordering/comments/whitespace, write back CRLF" editor,
already used by three call sites (`ApplyAccessibilityDefaults`,
`ApplyKeymapDefaults`, `ApplyKotor2WindowedDefaults`). `SpatialAudioManager.SetEaxValue`
implements the exact same algorithm — find `[Sound Options]` (creating it if
absent), scan for the `EAX` key, replace or append, write CRLF — independently,
in ~80 lines that are structurally the same walk (`FindSectionStart`-equivalent
loop, `sectionEnd` scan for the next `[...]` header, a body scan with
`IndexOf('=')`/`Substring`/`Trim`, an append-if-not-found tail, and the same
`StringBuilder` + `\r\n` + `UTF8Encoding(false)` write).

Why it's a problem: two independent implementations of the same ~40-line
algorithm in the same project is exactly the kind of duplication the brief
flags, and per this project's own stated convention ("before adding a helper,
search for an existing one — duplicate utilities are a recurring failure
mode across these accessibility projects") this is a case that convention is
meant to catch.

Proposed change: `SwkotorIniTweaker.ApplySectionPairs` is currently `private`
(SwkotorIniTweaker.cs:113). Making it `internal` and calling it from
`SpatialAudioManager.SetEaxValue(gameDir, enable)` as
`SwkotorIniTweaker.ApplySectionPairs(gameDir, "swkotor.ini", "[Sound Options]", new[] { ("EAX", enable ? "1" : "0") })`
removes the second implementation entirely. Note `ApplySectionPairs` currently
hardcodes `IniFileName = "swkotor.ini"` selection via its `iniFileName`
parameter, which already matches what `SpatialAudioManager` needs (it never
touches `swkotor2.ini`), so no generalization beyond visibility is required.

Risk: `low` (the two implementations were verified to do the same thing
cell-for-cell — same three cases: value already correct / value differs /
key missing — so swapping the call should be behavior-preserving). Needs a
before/after diff of a written `swkotor.ini`'s `[Sound Options]` section as
the mechanical check, plus the existing "toggle spatial audio" flow exercised
once (Enable then Disable) to confirm `EAX=1`/`EAX=0` still land correctly.
Estimated line delta: roughly -75.

## Findings (possible bugs — user decides)

None. Nothing found in this batch looked like a behavior bug distinct from
the accessibility gaps already reported in Section A (which the task brief
asked to be reported there, not here) or the duplication in Section B.

## Candidate 28 — narrow-header include opportunities

Does not apply to this batch. Candidate 28 is specific to the
`engine_offsets.h` C++ aggregator-header family in `patches/Accessibility/`;
this batch is C#/WinForms and has no equivalent aggregator-header pattern —
every file in the batch has a small, purpose-specific `using` list (verified
by inspection while reading each file; no unused or overly-broad `using`
directives were found in the batch).

## Files scanned with nothing to report

- ModSelection.cs
- GameVersionSelection.cs
- ModSelectionForm.cs
- GameVersionSelectionForm.cs
- Kotor2ModSelectionForm.cs
- ModdingInfoForm.cs (worth calling out positively: the read-only multiline
  `TextBox` used instead of a stack of non-focusable `Label`s specifically so
  a screen reader can arrow through the content, per its own doc comment —
  a pattern the batch's `Label`-only info screens could learn from, though no
  other screen in this batch has content long enough to need it)
- CancelConfirm.cs
- ModInstallerCoordinator.cs
- ModInstallContext.cs
- IModInstaller.cs
- ModInstallResult.cs
- ModInstallers/GitHubTslpatchdataFetcher.cs
- ModInstallers/HoloPatcherProvider.cs
- ModInstallers/TweakPackInstaller.cs
- TslrcmDetector.cs
