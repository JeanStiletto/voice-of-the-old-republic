namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// User's per-category selection from <see cref="ModSelectionForm"/>.
    /// Each flag covers one of the three optional mod groupings; the always-installed
    /// components (accessibility mod, KotorPatcher runtime, Prism, widescreen) live
    /// outside this struct because they are not user-selectable.
    /// </summary>
    public sealed class ModSelection
    {
        /// <summary>KOTOR 1 Community Patch + locale patch.</summary>
        public bool K1cp { get; init; } = true;

        /// <summary>Juhani Dialogue Restoration + Party Conversations on Ebon Hawk.</summary>
        public bool RestoredCutContent { get; init; } = true;

        /// <summary>Thematic KOTOR Companions + Swoop Bike Upgrades.</summary>
        public bool CompanionAndSwoopUpgrades { get; init; } = true;

        /// <summary>
        /// KOTOR 2 Community Patch. Only meaningful once the KOTOR 2 install
        /// flow is wired (see <see cref="ModInstallers.K2cpInstaller"/>); the
        /// KOTOR 1 flow's <see cref="ModSelectionForm"/> forces it false.
        /// </summary>
        public bool K2cp { get; init; } = true;

        public static ModSelection AllOn() => new();

        public override string ToString() =>
            $"K1cp={K1cp}, RestoredCutContent={RestoredCutContent}, " +
            $"CompanionAndSwoopUpgrades={CompanionAndSwoopUpgrades}, K2cp={K2cp}";
    }
}
