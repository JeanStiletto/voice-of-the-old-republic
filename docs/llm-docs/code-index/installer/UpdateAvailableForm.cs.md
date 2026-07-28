# UpdateAvailableForm.cs (85 lines)

Small WinForms dialog shown by `Program.cs` when a newer mod version is detected and the mod is already installed. Presents four buttons in one row — Update (mod only), Full install, Toggle spatial audio (label flips based on current dsoal state), Close — each setting `UserChoice` (a `UpdateChoice` enum value) and closing the form. `Program.Main` reads `UserChoice` after `Application.Run` returns to decide the next step.

## Declarations (in source order)

- L6 — `public class UpdateAvailableForm : Form`
- L8 — `public UpdateChoice UserChoice { get; private set; } = UpdateChoice.Close`
- L10 — `public UpdateAvailableForm(string installedVersion, string latestVersion, bool spatialAudioEnabled)`
- L15 — `private void InitializeComponents(string installedVersion, string latestVersion, bool spatialAudioEnabled)`
  note: toggle button's label swaps between `SpatialAudio_DisableButton`/`SpatialAudio_EnableButton` locale strings based on current state
