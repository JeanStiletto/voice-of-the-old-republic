using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Runs the selected mod installers in a fixed order: K1CP first (it edits
    /// dialog.tlk and other archives that subsequent mods build on top of), then
    /// the cut-content / mechanic mods, then exe-modifying mods (widescreen) last
    /// because they invalidate KPatchCore's SHA gate and need
    /// <see cref="KPatchCore.Applicators.PatchApplicator.InstallOptions.AllowVersionMismatch"/>
    /// flipped at the .kpatch step.
    ///
    /// Today only K1CP is wired up; the rest land as additional installers in
    /// <see cref="BuildPipeline"/> as we implement them.
    /// </summary>
    public static class ModInstallerCoordinator
    {
        public static IReadOnlyList<IModInstaller> BuildPipeline()
        {
            return new List<IModInstaller>
            {
                new K1cpInstaller(),
                // TODO: JdrInstaller, PartyConversationsInstaller, ThematicCompanionsInstaller,
                // SwoopBikeUpgradesInstaller, WidescreenInstaller.
            };
        }

        /// <summary>
        /// Run every installer whose <see cref="IModInstaller.IsSelected"/> returns true.
        /// Returns a per-mod result list. A failure from one installer does NOT short-
        /// circuit the rest — the user gets a per-mod summary at the end.
        /// </summary>
        public static async Task<List<ModInstallResult>> InstallSelectedAsync(
            IReadOnlyList<IModInstaller> pipeline,
            ModSelection selection,
            string gameDir,
            string holoPatcherExePath,
            Action<int> overallProgress,
            Action<string> statusUpdate)
        {
            var results = new List<ModInstallResult>();
            if (selection == null)
            {
                Logger.Info("ModInstallerCoordinator: no ModSelection (update-only path); skipping bundled mods.");
                return results;
            }

            var locale = GameLocaleDetector.Detect(gameDir);

            int selectedCount = 0;
            foreach (var inst in pipeline)
                if (inst.IsSelected(selection)) selectedCount++;

            if (selectedCount == 0)
            {
                Logger.Info("ModInstallerCoordinator: user deselected every optional mod; nothing to do.");
                return results;
            }

            int slotIndex = 0;
            foreach (var installer in pipeline)
            {
                if (!installer.IsSelected(selection))
                {
                    Logger.Info($"ModInstallerCoordinator: skipping {installer.Id} (not selected)");
                    results.Add(ModInstallResult.SkippedResult(installer.Id));
                    continue;
                }

                Logger.Info($"ModInstallerCoordinator: starting {installer.DisplayName} ({installer.Id})");

                // Each selected installer gets an equal slice of [0..100].
                int slotStart = slotIndex * 100 / selectedCount;
                int slotEnd = (slotIndex + 1) * 100 / selectedCount;
                int slotWidth = slotEnd - slotStart;
                int currentSlot = slotIndex;

                var ctx = new ModInstallContext
                {
                    GameDir = gameDir,
                    Locale = locale,
                    HoloPatcherExePath = holoPatcherExePath,
                    Progress = p =>
                    {
                        int clamped = Math.Max(0, Math.Min(100, p));
                        overallProgress?.Invoke(slotStart + (clamped * slotWidth / 100));
                    },
                    StatusUpdate = statusUpdate,
                };

                var result = await installer.InstallAsync(ctx);
                results.Add(result);

                if (result.Success)
                {
                    Logger.Info($"ModInstallerCoordinator: {installer.Id} OK");
                }
                else
                {
                    Logger.Warning($"ModInstallerCoordinator: {installer.Id} FAILED — {result.Error}");
                }

                slotIndex++;
            }

            return results;
        }
    }
}
