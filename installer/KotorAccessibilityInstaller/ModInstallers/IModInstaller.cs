using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// One per bundled optional mod (K1CP, JDR, Party Conversations on Ebon Hawk,
    /// Thematic Companions, Swoop Bike Upgrades, UniWS, HR Menus). Plays inside
    /// MainForm's install pipeline after the .kpatch + Prism + ini tweaks land.
    ///
    /// Contract: each installer is self-contained — it downloads its own source,
    /// stages it, applies it, cleans up. The coordinator handles ordering and
    /// per-mod selection via <see cref="ModSelection"/>.
    /// </summary>
    public interface IModInstaller
    {
        /// <summary>Short stable identifier (e.g. "k1cp"). Logged + used in error messages.</summary>
        string Id { get; }

        /// <summary>Human-readable display name (e.g. "KOTOR 1 Community Patch v1.10.1").</summary>
        string DisplayName { get; }

        /// <summary>
        /// True when the user's <see cref="ModSelection"/> has this mod toggled on.
        /// The coordinator skips installers whose <see cref="IsSelected"/> returns false.
        /// </summary>
        bool IsSelected(ModSelection selection);

        /// <summary>Perform the install. Throws nothing; reports failure via <see cref="ModInstallResult.Success"/>.</summary>
        Task<ModInstallResult> InstallAsync(ModInstallContext ctx);
    }
}
