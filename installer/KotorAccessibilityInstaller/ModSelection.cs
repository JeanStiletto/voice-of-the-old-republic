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

        /// <summary>Swoop Bike Upgrades (KOTOR 1). Not yet wired.</summary>
        public bool SwoopUpgrades { get; init; } = true;

        /// <summary>
        /// Thematic Companions, one flag per game because it is one decision per
        /// game: the two are separate mods by the same authors, each opt-in on
        /// its own game's mod-selection screen. Unlike every other entry here
        /// these default to FALSE — see
        /// <see cref="ModInstallers.ThematicCompanionsInstaller"/> for why a
        /// balance mod does not get to ride in on an all-on default.
        /// </summary>
        public bool ThematicCompanionsK1 { get; init; } = false;

        /// <inheritdoc cref="ThematicCompanionsK1"/>
        public bool ThematicCompanionsK2 { get; init; } = false;

        /// <summary>
        /// KOTOR 2 Community Patch. Set by <see cref="Kotor2ModSelectionForm"/>;
        /// the KOTOR 1 flow's <see cref="ModSelectionForm"/> forces it false.
        /// </summary>
        public bool K2cp { get; init; } = true;

        /// <summary>
        /// Unofficial TSLRCM Tweak Pack (KOTOR 2). Set by
        /// <see cref="Kotor2ModSelectionForm"/>; forced false on the KOTOR 1 flow.
        /// </summary>
        public bool TweakPack { get; init; } = true;

        /// <summary>
        /// Every grouping that defaults on. Thematic Companions is deliberately
        /// NOT included: it is opt-in per game, so "all on" means all of the
        /// recommended set, not literally every flag.
        /// </summary>
        public static ModSelection AllOn() => new();

        public override string ToString() =>
            $"K1cp={K1cp}, RestoredCutContent={RestoredCutContent}, " +
            $"SwoopUpgrades={SwoopUpgrades}, K2cp={K2cp}, TweakPack={TweakPack}, " +
            $"ThematicCompanionsK1={ThematicCompanionsK1}, ThematicCompanionsK2={ThematicCompanionsK2}";
    }
}
