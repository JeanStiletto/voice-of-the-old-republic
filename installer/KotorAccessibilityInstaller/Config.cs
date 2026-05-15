namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Configuration constants for the installer.
    /// Update these values before building a release.
    /// </summary>
    public static class Config
    {
        /// <summary>
        /// GitHub repository URL for the KOTOR Accessibility mod.
        /// Format: "https://github.com/username/repo"
        /// </summary>
        public const string ModRepositoryUrl = "https://github.com/JeanStiletto/kotor-accessibility";

        /// <summary>
        /// GitHub Pages site URL. Used for opening the README without the
        /// surrounding GitHub repo chrome. Must end with a trailing slash.
        /// </summary>
        public const string ModSiteUrl = "https://jeanstiletto.github.io/kotor-accessibility/";

        /// <summary>
        /// Filename of the .kpatch artifact uploaded to GitHub releases.
        /// </summary>
        public const string KPatchAssetName = "Accessibility.kpatch";

        /// <summary>
        /// Patch ID inside the .kpatch — used by KPatchCore to look it up.
        /// Must match the manifest.toml inside patches/Accessibility/.
        /// </summary>
        public const string PatchId = "accessibility";

        /// <summary>
        /// Publisher name for registry entries.
        /// </summary>
        public const string Publisher = "KOTOR Accessibility Project";

        /// <summary>
        /// Display name for Add/Remove Programs.
        /// </summary>
        public const string DisplayName = "KOTOR Accessibility Mod";

        /// <summary>
        /// Filename of the persistent uninstaller copied into the game folder
        /// at install time so Add/Remove Programs keeps working after the
        /// original download is deleted.
        /// </summary>
        public const string UninstallerExeName = "KotorAccessibility_Uninstaller.exe";
    }
}
