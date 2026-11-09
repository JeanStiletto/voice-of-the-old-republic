using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Appends the mod's dedicated full-volume audio priority group to
    /// <c>prioritygroups.2da</c>.
    ///
    /// <para>Why a new row instead of editing an existing group: the mod's
    /// audio cues need a Volume=127 (unity) group, but the legacy group they
    /// used (group 0) is Volume=106 (~83%). Editing an existing group would
    /// change every engine sound assigned to it. Appending a fresh row leaves
    /// all vanilla groups untouched.</para>
    ///
    /// <para>Index-drift safety: the row index can't be hard-coded — another
    /// mod that also extends this table could land its rows before ours. The
    /// accessibility DLL finds our group at runtime by a sentinel value in the
    /// <c>FadeTime</c> column (<see cref="SentinelFadeTime"/>; vanilla fade
    /// times are only 0 or 1000), so it works wherever the row ends up.</para>
    ///
    /// <para>Append preserves every existing byte of the offset table and data
    /// block: row_count grows by one, one row label and col_count offsets are
    /// appended, the new cells' strings go on the end of the data block, and
    /// data_size is bumped. Existing offsets stay valid because the block only
    /// grows at the tail. File layout: <see cref="TwoDa"/>.</para>
    /// </summary>
    public static class PriorityGroup2da
    {
        /// <summary>Fingerprint stamped into the flat full-volume row's FadeTime column.</summary>
        public const ushort SentinelFadeTime = 31337;

        /// <summary>Fingerprint for the near-field "spatial" row (tight falloff band).</summary>
        public const ushort SpatialSentinelFadeTime = 31338;

        // Appended row values by column. "label" is empty (every row's is).
        //
        // Flat full-volume group: clone of vanilla group 0 (priority 0, 4 voices,
        // 10m full-volume radius / 20m floor, no playback variance) but Volume=127
        // (full) and FadeTime=sentinel. Interrupt=1 so a fresh cue preempts an old
        // one when the voice budget is full rather than being dropped. Used by the
        // on-demand cues (cycling, beacon, combat) that may target distant objects
        // and just need to stay audible at range.
        private static readonly Dictionary<string, string> FlatRow =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "priority", "0" },
                { "volume", "127" },
                { "maxplaying", "4" },
                { "interrupt", "1" },
                { "fadetime", SentinelFadeTime.ToString() },
                { "maxvolumedist", "10" },
                { "minvolumedist", "20" },
                { "playbackvariance", "0" },
            };

        // Near-field "spatial" group: same Volume=127 but a TIGHT falloff band —
        // full volume only within 1m, ramping to the floor by 8m. This restores a
        // distance-loudness gradient across the passive proximity cues' ~5m
        // awareness range (the flat group is full-volume out to 10m, so cues fired
        // within 5m never varied with distance). Mirrors the engine's own
        // near-field SFX groups (e.g. vanilla groups 19/20: full to 4m, floor 8m),
        // compressed tighter to fit our shorter awareness range. Used only by the
        // passive wall/door/container/NPC/item/transition cues that funnel through
        // PlayCueAtPosition. One-shot cues take their falloff band from their
        // priority group (the one-shot engine API has no per-source distance), so
        // a dedicated group is the only way to give them a band of their own.
        private static readonly Dictionary<string, string> SpatialRow =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "priority", "0" },
                { "volume", "127" },
                { "maxplaying", "4" },
                { "interrupt", "1" },
                { "fadetime", SpatialSentinelFadeTime.ToString() },
                { "maxvolumedist", "1" },
                { "minvolumedist", "8" },
                { "playbackvariance", "0" },
            };

        // Every accessibility row we maintain, paired with its sentinel.
        private static readonly (ushort Sentinel, Dictionary<string, string> Row)[] AccRows =
            new[]
            {
                (SentinelFadeTime, FlatRow),
                (SpatialSentinelFadeTime, SpatialRow),
            };

        private static int FadeTimeCol(TwoDa.Table t) => TwoDa.ColumnIndex(t, "fadetime");

        /// <summary>True if the parsed table carries a row with the given sentinel.</summary>
        private static bool HasSentinel(TwoDa.Table t, int fadeCol, ushort sentinel)
        {
            if (fadeCol < 0) return false;
            string s = sentinel.ToString();
            for (int r = 0; r < t.RowCount; r++)
                if (TwoDa.Cell(t, r, fadeCol) == s) return true;
            return false;
        }

        /// <summary>True if the table already carries ALL of our sentinel rows.</summary>
        public static bool HasAccGroup(byte[] buf)
        {
            var t = TwoDa.Parse(buf);
            int fc = FadeTimeCol(t);
            if (fc < 0) return false;
            foreach (var (sentinel, _) in AccRows)
                if (!HasSentinel(t, fc, sentinel)) return false;
            return true;
        }

        // Per-platform column suffixes. KOTOR 2's prioritygroups.2da splits three
        // of KOTOR 1's columns per target platform — volume_pc / volume_xbox,
        // maxvolumedist_pc / _xbox, minvolumedist_pc / _xbox — while priority,
        // maxplaying, interrupt, fadetime and playbackvariance stay unsuffixed.
        //
        // Mapping the suffix away rather than keeping a per-game column table
        // means the row definitions above stay the single description of what
        // our groups ARE, and both platform variants get the same value. That
        // matters because writing the KOTOR 1 names into the KOTOR 2 table is
        // not a loud failure: the cells simply come out empty, and the group
        // lands without its volume or falloff band.
        private static readonly string[] PlatformSuffixes = { "_pc", "_xbox" };

        private static string LogicalColumn(string column)
        {
            foreach (var suffix in PlatformSuffixes)
            {
                if (column.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
                    return column.Substring(0, column.Length - suffix.Length);
            }
            return column;
        }

        // Appends one row (dictionary keyed by logical column name) to the parsed table.
        private static void AppendRow(TwoDa.Table t, Dictionary<string, string> row)
        {
            int newIdx = t.RowCount;
            var data = new List<byte>(t.DataBlock);

            foreach (var c in t.Cols)
            {
                // No special case for the "label" column: it is absent from the
                // row definitions, so it falls through to the empty-cell path
                // below and gets a real empty string appended to the data block.
                //
                // It used to point at data offset 0 on the assumption that a
                // shared empty string lived there. It does not — offset 0 is
                // where row 0's first cell starts, so every appended row came
                // out labelled "Unmaskable_Sound", a duplicate of vanilla group
                // 0's name. Harmless in practice (our groups are found by the
                // sentinel FadeTime, never by name) but wrong, and a duplicate
                // name is exactly the sort of thing a label-based lookup
                // elsewhere would trip over.
                row.TryGetValue(LogicalColumn(c), out string? val);
                val ??= "";  // unknown column -> empty cell (defensive, forward-compat)
                int off = data.Count;
                data.AddRange(Encoding.ASCII.GetBytes(val));
                data.Add(0);
                t.Offsets.Add((ushort)off);
            }

            t.RowCount += 1;
            t.Labels.Add(newIdx.ToString());
            t.DataBlock = data.ToArray();
        }

        /// <summary>
        /// Returns the 2da bytes with every accessibility row appended that isn't
        /// already present. Idempotent: returns <paramref name="source"/> unchanged
        /// when all our rows are already there (so callers can detect "no change"
        /// via reference equality). Adds only the missing rows otherwise — an
        /// install that already has the flat row but not the spatial row gains just
        /// the spatial one. Throws on a malformed/unrecognised 2da.
        /// </summary>
        public static byte[] AppendAccGroup(byte[] source)
        {
            var t = TwoDa.Parse(source);
            int fc = FadeTimeCol(t);

            bool changed = false;
            foreach (var (sentinel, row) in AccRows)
            {
                if (HasSentinel(t, fc, sentinel)) continue;
                AppendRow(t, row);
                changed = true;
            }
            if (!changed) return source;

            byte[] outBytes = TwoDa.Build(t);

            // Self-check: every row must read back from the freshly written bytes.
            if (!HasAccGroup(outBytes))
                throw new InvalidDataException(
                    "prioritygroups.2da append self-check failed (a sentinel row is missing from output)");
            return outBytes;
        }
    }
}
