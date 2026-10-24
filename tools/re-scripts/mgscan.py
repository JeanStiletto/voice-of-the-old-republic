"""Scan KOTOR module archives (.mod ERF preferred over .rim) for .are files
with a MiniGame struct; dump the MiniGame struct (and area-level script
fields) as indented text.

usage: py mgscan.py <modules_dir> <out_dir> [--all]
"""
import os, struct, sys

ARE = 2012

def read_erf(data):
    sig = data[:4]
    if sig in (b'ERF ', b'MOD ', b'SAV ', b'HAK '):
        (lang_count, lang_size, entry_count, off_lang, off_keys, off_res) = struct.unpack_from('<IIIIII', data, 8)
        out = {}
        for i in range(entry_count):
            resref, resid, rtype, _ = struct.unpack_from('<16sIHH', data, off_keys + i * 24)
            off, size = struct.unpack_from('<II', data, off_res + resid * 8)
            out[(resref.split(b'\0')[0].decode('latin-1').lower(), rtype)] = data[off:off + size]
        return out
    if sig == b'RIM ':
        (_, entry_count, off_keys) = struct.unpack_from('<III', data, 8)
        out = {}
        for i in range(entry_count):
            resref, rtype, resid, off, size = struct.unpack_from('<16sIIII', data, off_keys + i * 32)
            out[(resref.split(b'\0')[0].decode('latin-1').lower(), rtype)] = data[off:off + size]
        return out
    return {}

class Gff:
    def __init__(self, d):
        self.d = d
        h = struct.unpack_from('<4s4s12I', d, 0)
        (_, _, self.so, self.sc, self.fo, self.fc, self.lo, self.lc,
         self.fdo, self.fdc, self.fio, self.fic, self.lio, self.lic) = h
    def label(self, i):
        return self.d[self.lo + i * 16:self.lo + i * 16 + 16].split(b'\0')[0].decode('latin-1')
    def struct_fields(self, si):
        stype, dofs, cnt = struct.unpack_from('<III', self.d, self.so + si * 12)
        if cnt == 1:
            idx = [dofs]
        else:
            idx = list(struct.unpack_from('<%dI' % cnt, self.d, self.fio + dofs)) if cnt else []
        return stype, idx
    def field(self, fi):
        ftype, li, dv = struct.unpack_from('<III', self.d, self.fo + fi * 12)
        return ftype, self.label(li), dv
    def value(self, ftype, dv):
        d = self.d; fd = self.fdo
        if ftype == 0: return dv & 0xff
        if ftype == 1: return struct.unpack('<b', struct.pack('<B', dv & 0xff))[0]
        if ftype == 2: return dv & 0xffff
        if ftype == 3: return struct.unpack('<h', struct.pack('<H', dv & 0xffff))[0]
        if ftype == 4: return dv
        if ftype == 5: return struct.unpack('<i', struct.pack('<I', dv))[0]
        if ftype == 6: return struct.unpack_from('<Q', d, fd + dv)[0]
        if ftype == 7: return struct.unpack_from('<q', d, fd + dv)[0]
        if ftype == 8: return round(struct.unpack('<f', struct.pack('<I', dv))[0], 4)
        if ftype == 9: return struct.unpack_from('<d', d, fd + dv)[0]
        if ftype == 10:
            n = struct.unpack_from('<I', d, fd + dv)[0]
            return d[fd + dv + 4:fd + dv + 4 + n].decode('latin-1')
        if ftype == 11:
            n = d[fd + dv]
            return d[fd + dv + 1:fd + dv + 1 + n].decode('latin-1')
        if ftype == 12:
            total, strref, cnt = struct.unpack_from('<IiI', d, fd + dv)
            p = fd + dv + 12; subs = []
            for _ in range(cnt):
                sid, ln = struct.unpack_from('<II', d, p)
                subs.append(d[p + 8:p + 8 + ln].decode('cp1252', 'replace')); p += 8 + ln
            return 'strref=%d %s' % (strref, subs)
        if ftype == 13:
            n = struct.unpack_from('<I', d, fd + dv)[0]
            return '<void %d bytes>' % n
        if ftype == 16: return struct.unpack_from('<4f', d, fd + dv)
        if ftype == 17: return tuple(round(x, 3) for x in struct.unpack_from('<3f', d, fd + dv))
        return '?type%d' % ftype
    def dump(self, si, ind, out, depth=0):
        _, idx = self.struct_fields(si)
        for fi in idx:
            ftype, lab, dv = self.field(fi)
            if ftype == 14:
                out.append('%s%s:' % (ind, lab)); self.dump(dv, ind + '  ', out, depth + 1)
            elif ftype == 15:
                n = struct.unpack_from('<I', self.d, self.lio + dv)[0]
                items = struct.unpack_from('<%dI' % n, self.d, self.lio + dv + 4) if n else ()
                out.append('%s%s: [%d]' % (ind, lab, n))
                for k, s in enumerate(items):
                    out.append('%s  #%d' % (ind, k)); self.dump(s, ind + '    ', out, depth + 1)
            else:
                out.append('%s%s = %s' % (ind, lab, self.value(ftype, dv)))
    def find_top(self, name):
        _, idx = self.struct_fields(0)
        for fi in idx:
            ftype, lab, dv = self.field(fi)
            if lab == name: return ftype, dv
        return None

def main():
    mdir, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    names = {}
    for fn in os.listdir(mdir):
        base, ext = os.path.splitext(fn)
        ext = ext.lower()
        if ext not in ('.mod', '.rim') or base.lower().endswith('_s') or base.lower().endswith('_dlg'):
            continue
        key = base.lower()
        if ext == '.mod' or key not in names:
            names[key] = fn
    for key, fn in sorted(names.items()):
        res = read_erf(open(os.path.join(mdir, fn), 'rb').read())
        for (rr, rt), data in res.items():
            if rt != ARE: continue
            g = Gff(data)
            mg = g.find_top('MiniGame')
            if not mg: continue
            lines = []
            g.dump(mg[1], '  ', lines)
            typ = next((l for l in lines if l.strip().startswith('Type =')), '?')
            print('%-14s %-12s %s' % (fn, rr, typ.strip()))
            top = []
            _, idx = g.struct_fields(0)
            for fi in idx:
                ftype, lab, dv = g.field(fi)
                if ftype not in (14, 15) and (lab.startswith('On') or lab in ('Name', 'Tag')):
                    top.append('  %s = %s' % (lab, g.value(ftype, dv)))
            with open(os.path.join(out, '%s__%s.txt' % (key, rr)), 'w', encoding='utf-8') as f:
                f.write('source %s\n[area]\n' % fn + '\n'.join(top) + '\n[MiniGame]\n' + '\n'.join(lines) + '\n')

main()
