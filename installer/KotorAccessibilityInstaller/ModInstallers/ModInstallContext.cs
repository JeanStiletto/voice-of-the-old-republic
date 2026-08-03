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

        /// <summary>
        /// Ask the user to supply a file the installer could not fetch itself,
        /// returning the path they chose or null if they declined. Null when the
        /// caller has no UI to ask with, in which case installers must treat a
        /// failed download as a plain failure.
        ///
        /// <para>Injected rather than called directly so installers stay free of
        /// WinForms: they describe what they need, the host decides how to ask.
        /// The host is also responsible for marshalling to the UI thread — this
        /// is invoked from an installer's async body.</para>
        /// </summary>
        public Func<ManualDownloadRequest, string> AskForManualDownload { get; init; }
    }

    /// <summary>
    /// What an installer needs the user to fetch by hand. See
    /// <see cref="ManualDownloadForm"/> for why a user-supplied file is accepted
    /// without a hash check.
    /// </summary>
    public sealed class ManualDownloadRequest
    {
        /// <summary>Name + version as the user would recognise it on the mod page.</summary>
        public string ModDisplayName { get; init; }

        /// <summary>Localized explanation of why the automatic download did not work.</summary>
        public string Reason { get; init; }

        /// <summary>Page to open in the browser.</summary>
        public string PageUrl { get; init; }

        /// <summary>Filename upstream serves, used to spot the file in Downloads.</summary>
        public string ExpectedFileName { get; init; }

        /// <summary>Shape check, so a mis-picked file fails immediately and clearly.</summary>
        public ManualDownloadForm.FileKind Kind { get; init; }

        /// <summary>Pin to compare against for the log only — never a gate here.</summary>
        public string PinnedSha256 { get; init; }
    }
}
