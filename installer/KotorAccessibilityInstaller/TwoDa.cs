using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Reads and writes binary 2DA tables (the engine's <c>2DA V2.b</c> format)
    /// for the installer's in-place table patches.
    ///
    /// <para>Layout:
    /// <code>
    /// "2DA V2.b\n"
    /// &lt;col header&gt;\t ... \t \0
    /// uint32 row_count
    /// &lt;row label&gt;\t ...            (one tab-terminated token per row)
    /// uint16 cell_offset[row_count*col_count]   (row-major, offset into data block)
    /// uint16 data_size
    /// &lt;data block&gt;                  (NUL-terminated strings)
    /// </code>
    /// Edits never rewrite existing strings: a changed cell gets its new string
    /// appended to the data block and its offset repointed, so every other cell's
    /// offset stays valid. The old string is left orphaned, which the engine
    /// ignores.</para>
    /// </summary>
    internal static class TwoDa
    {
        private static readonly byte[] Magic = Encoding.ASCII.GetBytes("2DA V2.b\n");

        internal sealed class Table
        {
            public List<string> Cols;
            public int RowCount;
            public List<string> Labels;
            public List<ushort> Offsets;
            public byte[] DataBlock;
        }

        internal static Table Parse(byte[] buf)
        {
            if (buf.Length < Magic.Length)
                throw new InvalidDataException("2da too short");
            for (int i = 0; i < Magic.Length; i++)
                if (buf[i] != Magic[i])
                    throw new InvalidDataException("not a 2DA V2.b file");

            int p = Magic.Length;
            int hdrEnd = Array.IndexOf(buf, (byte)0, p);
            if (hdrEnd < 0) throw new InvalidDataException("2da header not NUL-terminated");
            var cols = new List<string>(
                Encoding.ASCII.GetString(buf, p, hdrEnd - p).Split('\t'));
            if (cols.Count > 0 && cols[cols.Count - 1].Length == 0)
                cols.RemoveAt(cols.Count - 1);  // trailing tab leaves an empty token

            p = hdrEnd + 1;
            int rowCount = BitConverter.ToInt32(buf, p);
            p += 4;

            var labels = new List<string>(rowCount);
            for (int r = 0; r < rowCount; r++)
            {
                int t = Array.IndexOf(buf, (byte)'\t', p);
                if (t < 0) throw new InvalidDataException("2da row label not terminated");
                labels.Add(Encoding.ASCII.GetString(buf, p, t - p));
                p = t + 1;
            }

            int ncells = rowCount * cols.Count;
            var offsets = new List<ushort>(ncells);
            for (int i = 0; i < ncells; i++) { offsets.Add(BitConverter.ToUInt16(buf, p)); p += 2; }

            int dataSize = BitConverter.ToUInt16(buf, p);
            p += 2;
            if (p + dataSize > buf.Length)
                throw new InvalidDataException("2da data block runs past EOF");
            var data = new byte[dataSize];
            Array.Copy(buf, p, data, 0, dataSize);

            return new Table { Cols = cols, RowCount = rowCount, Labels = labels,
                               Offsets = offsets, DataBlock = data };
        }

        internal static byte[] Build(Table t)
        {
            // data_size is a uint16 on disk; a silently truncated size would
            // cut the table off mid-string instead of failing here.
            if (t.DataBlock.Length > ushort.MaxValue)
                throw new InvalidDataException("2da data block exceeds 64 KB");

            using var ms = new MemoryStream();
            ms.Write(Magic, 0, Magic.Length);
            byte[] hdr = Encoding.ASCII.GetBytes(string.Join("\t", t.Cols) + "\t");
            ms.Write(hdr, 0, hdr.Length);
            ms.WriteByte(0);
            ms.Write(BitConverter.GetBytes(t.RowCount), 0, 4);
            foreach (var lbl in t.Labels)
            {
                byte[] lb = Encoding.ASCII.GetBytes(lbl);
                ms.Write(lb, 0, lb.Length);
                ms.WriteByte((byte)'\t');
            }
            foreach (var off in t.Offsets) ms.Write(BitConverter.GetBytes(off), 0, 2);
            ms.Write(BitConverter.GetBytes((ushort)t.DataBlock.Length), 0, 2);
            ms.Write(t.DataBlock, 0, t.DataBlock.Length);
            return ms.ToArray();
        }

        internal static string Cell(Table t, int row, int colIdx)
        {
            int off = t.Offsets[row * t.Cols.Count + colIdx];
            int end = Array.IndexOf(t.DataBlock, (byte)0, off);
            return Encoding.ASCII.GetString(t.DataBlock, off, end - off);
        }

        /// <summary>Index of the named column (case-insensitive, as the engine matches), or -1.</summary>
        internal static int ColumnIndex(Table t, string name) =>
            t.Cols.FindIndex(c => c.Equals(name, StringComparison.OrdinalIgnoreCase));

        /// <summary>Appends <paramref name="value"/> to the data block and returns its offset.</summary>
        internal static ushort AppendString(Table t, string value)
        {
            int off = t.DataBlock.Length;
            byte[] bytes = Encoding.ASCII.GetBytes(value);
            var data = new byte[off + bytes.Length + 1];  // + NUL
            Array.Copy(t.DataBlock, data, off);
            Array.Copy(bytes, 0, data, off, bytes.Length);
            t.DataBlock = data;
            return (ushort)off;
        }

        internal static void SetCell(Table t, int row, int colIdx, string value) =>
            t.Offsets[row * t.Cols.Count + colIdx] = AppendString(t, value);

        /// <summary>
        /// Appends a column named <paramref name="name"/> whose every cell is the
        /// same string as column <paramref name="sourceCol"/>'s. The cells share
        /// the source's strings, so the data block does not grow.
        /// </summary>
        internal static void AddColumnCopy(Table t, string name, int sourceCol)
        {
            int oldCount = t.Cols.Count;
            var offsets = new List<ushort>(t.RowCount * (oldCount + 1));
            for (int r = 0; r < t.RowCount; r++)
            {
                for (int c = 0; c < oldCount; c++)
                    offsets.Add(t.Offsets[r * oldCount + c]);
                offsets.Add(t.Offsets[r * oldCount + sourceCol]);
            }
            t.Cols.Add(name);
            t.Offsets = offsets;
        }
    }
}
