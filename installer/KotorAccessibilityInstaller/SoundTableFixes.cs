using System;
using System.Collections.Generic;
using System.IO;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Repairs vanilla 2DA data that leaves existing game sounds silent. Every
    /// fix points the engine at a sound file the game already ships; none adds
    /// or replaces audio.
    ///
    /// <para><b>Shield-off sound (both games).</b> When a visual effect ends,
    /// the engine looks up the column <c>SoundCessation</c> in
    /// <c>visualeffects.2da</c>. The shipped table spells it
    /// <c>soundcessastion</c>, so the lookup fails, falls back to an empty name
    /// and plays nothing. The column's only filled rows are the energy shields,
    /// whose <c>gen_shieldbluoff</c> / <c>gen_shieldredoff</c> sounds sit unused
    /// in <c>sounds.bif</c>. (<c>vfx_persistent.2da</c> spells the column
    /// correctly, so the typo is local to this table.)</para>
    ///
    /// <para>The fix ADDS a correctly spelled column sharing the old column's
    /// cells rather than renaming it: a patcher-based mod installed later that
    /// edits rows by the old column name keeps finding it.</para>
    ///
    /// <para><b>Two KOTOR 1 cells naming files that do not exist.</b> KOTOR 2's
    /// shipped tables already carry these exact corrections, which is the
    /// source of the replacement values:
    /// <list type="bullet">
    /// <item>Plastic crate "locked" sound: <c>pl_plastsm_lock</c> becomes
    /// <c>pl_plastic_lock</c>, the sound the other plastic containers use.</item>
    /// <item>Sonic ammunition impact: <c>cb_ht_sonblclth1</c> becomes
    /// <c>cb_ht_hvyblleth1</c>. No sonic impact sound exists in either
    /// game.</item>
    /// </list></para>
    ///
    /// <para>Cell fixes match on the known-bad VALUE, not on a row index: a
    /// table that another mod already changed, or one that is already correct
    /// (all of KOTOR 2's), is left untouched, and re-running is a no-op.</para>
    /// </summary>
    public static class SoundTableFixes
    {
        private sealed record ColumnAlias(string Table, string Misspelled, string Correct);

        private sealed record CellFix(string Table, string[] Columns, string Bad, string Good, string What);

        private static readonly ColumnAlias[] ColumnAliases =
        {
            new ColumnAlias("visualeffects.2da", "soundcessastion", "soundcessation"),
        };

        private static readonly CellFix[] CellFixes =
        {
            new CellFix("placeableobjsnds.2da", new[] { "locked" },
                        "pl_plastsm_lock", "pl_plastic_lock", "plastic crate locked sound"),
            new CellFix("ammunitiontypes.2da", new[] { "impactsound0", "impactsound1" },
                        "cb_ht_sonblclth1", "cb_ht_hvyblleth1", "sonic ammunition impact sound"),
        };

        /// <summary>
        /// Returns <paramref name="source"/> with every fix for
        /// <paramref name="tableName"/> applied. Returns the same reference when
        /// nothing needed fixing, so callers can detect "no change". Each applied
        /// fix is described in <paramref name="applied"/> for the install log.
        /// Throws on a malformed 2da.
        /// </summary>
        public static byte[] Apply(string tableName, byte[] source, List<string> applied)
        {
            var t = TwoDa.Parse(source);
            bool changed = false;

            foreach (var alias in ColumnAliases)
            {
                if (!alias.Table.Equals(tableName, StringComparison.OrdinalIgnoreCase)) continue;
                int bad = TwoDa.ColumnIndex(t, alias.Misspelled);
                if (bad < 0 || TwoDa.ColumnIndex(t, alias.Correct) >= 0) continue;
                TwoDa.AddColumnCopy(t, alias.Correct, bad);
                applied.Add($"added column '{alias.Correct}' (copy of misspelled '{alias.Misspelled}')");
                changed = true;
            }

            foreach (var fix in CellFixes)
            {
                if (!fix.Table.Equals(tableName, StringComparison.OrdinalIgnoreCase)) continue;
                foreach (var column in fix.Columns)
                {
                    int col = TwoDa.ColumnIndex(t, column);
                    if (col < 0) continue;
                    for (int r = 0; r < t.RowCount; r++)
                    {
                        if (!TwoDa.Cell(t, r, col).Equals(fix.Bad, StringComparison.OrdinalIgnoreCase))
                            continue;
                        TwoDa.SetCell(t, r, col, fix.Good);
                        applied.Add($"{fix.What}: row {t.Labels[r]} {column} '{fix.Bad}' -> '{fix.Good}'");
                        changed = true;
                    }
                }
            }

            if (!changed) return source;

            byte[] outBytes = TwoDa.Build(t);

            // Self-check: the written bytes must parse and need no further fixing.
            var recheck = new List<string>();
            if (!ReferenceEquals(Apply(tableName, outBytes, recheck), outBytes))
                throw new InvalidDataException(
                    $"{tableName} sound-fix self-check failed: {string.Join("; ", recheck)}");
            return outBytes;
        }
    }
}
