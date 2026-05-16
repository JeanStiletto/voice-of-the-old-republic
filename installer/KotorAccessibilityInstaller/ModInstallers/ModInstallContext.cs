using System;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Per-install context handed to each <see cref="IModInstaller"/> by the
    /// coordinator. Holds the game path, detected locale, HoloPatcher driver
    /// path (extracted from the bundled resource), and UI hooks for progress
    /// + status updates.
    /// </summary>
    public sealed class ModInstallContext
    {
        /// <summary>Absolute path to the KOTOR install root.</summary>
        public string GameDir { get; init; }

        /// <summary>Detected game locale via dialog.tlk header.</summary>
        public GameLocale Locale { get; init; }

        /// <summary>
        /// Absolute path to a HoloPatcher.exe instance the installer can drive.
        /// Lazy-populated by <see cref="HoloPatcherProvider"/> on first use.
        /// </summary>
        public string HoloPatcherExePath { get; init; }

        /// <summary>0..100 progress callback for the current installer's slice of the bar.</summary>
        public Action<int> Progress { get; init; }

        /// <summary>Status text update (e.g. "Downloading K1CP from GitHub...").</summary>
        public Action<string> StatusUpdate { get; init; }
    }
}
