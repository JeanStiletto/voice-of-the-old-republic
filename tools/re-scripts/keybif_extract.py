"""Minimal KEY/BIF extractor: pull resources by resref substring + type list.
usage: keybif_extract.py <game_root> <out_dir> <resref_substring> <type,type,...>
types: lyt=3000 mdl=2002 mdx=3008 wav=4 tpc=3007 are=2012
"""
import os, struct, sys
root, out, needle, types = sys.argv[1], sys.argv[2], sys.argv[3].lower(), sys.argv[4]
typemap = {'lyt':3000,'mdl':2002,'mdx':3008,'wav':4,'tpc':3007,'are':2012,'utc':2027,'ncs':2010,'2da':2017}
want = {typemap[t] for t in types.split(',')}
inv = {v:k for k,v in typemap.items()}
key = open(os.path.join(root,'chitin.key'),'rb').read()
sig, ver, nbif, nkey, offbif, offkey = struct.unpack_from('<4s4sIIII', key, 0)
bifs = []
for i in range(nbif):
    size, nameoff, namelen, drives = struct.unpack_from('<IIHH', key, offbif + i*12)
    name = key[nameoff:nameoff+namelen].split(b'\0')[0].decode('latin-1')
    bifs.append(name.replace('\\','/'))
hits = []
for i in range(nkey):
    resref, rtype, resid = struct.unpack_from('<16sHI', key, offkey + i*22)
    resref = resref.split(b'\0')[0].decode('latin-1')
    if needle in resref.lower() and rtype in want:
        hits.append((resref, rtype, resid >> 20, resid & 0xFFFFF))
os.makedirs(out, exist_ok=True)
opened = {}
for resref, rtype, bifidx, residx in hits:
    path = os.path.join(root, bifs[bifidx])
    if path not in opened:
        opened[path] = open(path, 'rb')
    f = opened[path]
    f.seek(0); hdr = f.read(20)
    sig, ver, nvar, nfix, offvar = struct.unpack_from('<4s4sIII', hdr, 0)
    f.seek(offvar + residx*16)
    rid, off, size, rt = struct.unpack_from('<IIII', f.read(16), 0)
    f.seek(off); data = f.read(size)
    fn = os.path.join(out, f"{resref}.{inv.get(rtype, str(rtype))}")
    open(fn, 'wb').write(data)
    print(f"{resref}.{inv.get(rtype)} {size} bytes from {bifs[bifidx]}")
print(len(hits), "resources")
