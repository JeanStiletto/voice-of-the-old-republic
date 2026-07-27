namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// User's choice from <see cref="GameVersionSelectionForm"/>: which game(s)
    /// the installer should target. KOTOR 1 is the fully supported path; KOTOR 2
    /// support is in preparation (see docs/installer.md, "KOTOR 2 mod bundle").
    /// </summary>
    public sealed class GameVersionSelection
    {
        /// <summary>Star Wars: Knights of the Old Republic (swkotor.exe).</summary>
        public bool Kotor1 { get; init; } = true;

        /// <summary>Star Wars: Knights of the Old Republic II (swkotor2.exe).</summary>
        public bool Kotor2 { get; init; } = false;

        public override string ToString() => $"Kotor1={Kotor1}, Kotor2={Kotor2}";
    }
}
