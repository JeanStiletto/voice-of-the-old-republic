using System.Collections.Generic;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Outcome of a single <see cref="IModInstaller"/> run. The coordinator
    /// collects these so the install pipeline can surface a per-mod summary.
    /// </summary>
    public sealed class ModInstallResult
    {
        /// <summary>Mod ID this result is for (matches <see cref="IModInstaller.Id"/>).</summary>
        public string Id { get; init; }

        /// <summary>True when the install ran AND completed without errors.</summary>
        public bool Success { get; init; }

        /// <summary>True when the user opted out of this mod — not a failure.</summary>
        public bool Skipped { get; init; }

        /// <summary>Error message if <see cref="Success"/> is false.</summary>
        public string Error { get; init; }

        /// <summary>Informational messages (per-step status from the installer).</summary>
        public List<string> Messages { get; init; } = new();

        public static ModInstallResult Ok(string id) => new() { Id = id, Success = true };
        public static ModInstallResult SkippedResult(string id) => new() { Id = id, Skipped = true };
        public static ModInstallResult Fail(string id, string error) => new() { Id = id, Success = false, Error = error };
    }
}
