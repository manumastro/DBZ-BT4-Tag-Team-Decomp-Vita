"""Extracts from a BT4 B14 Rev2 ISO (Team BT4's, English or Spanish) the options screen as the Vita patches it: member 453
of PZS4US1.AFS, its section 1 unpacked, to <outdir>/opt_s1.bin, the input of gen_patches.py (with no boot.bin there: no
icons; no vs_s1.bin: no versus menu). The archives' tables are in BIN/DBZ4.BIN (as port/vita/vita_file.c reads them).
The output is the disc's own data: keep it out of the repository (port/build/).
usage: python3 isoopt.py <iso> <outdir>"""
import struct, sys, os

iso = open(sys.argv[1], 'rb')
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
SEC = 2048


def rd(lba, n):
    iso.seek(lba * SEC)
    return iso.read(n)


pvd = rd(16, SEC)
root = struct.unpack('<I', pvd[158:162])[0]
rootlen = struct.unpack('<I', pvd[166:170])[0]


def listdir(lba, length):
    data = rd(lba, length)
    res = {}
    i = 0
    while i < len(data):
        l = data[i]
        if l == 0:
            i = (i // SEC + 1) * SEC
            if i >= len(data):
                break
            continue
        loc = struct.unpack('<I', data[i + 2:i + 6])[0]
        ln = struct.unpack('<I', data[i + 10:i + 14])[0]
        nl = data[i + 32]
        name = data[i + 33:i + 33 + nl].decode('latin1').split(';')[0]
        res[name] = (loc, ln)
        i += l
    return res


rootd = listdir(root, rootlen)
datad = listdir(*rootd['DATA'])
bind = listdir(*rootd['BIN'])
loc, ln = bind['DBZ4.BIN']
db = rd(loc, ln)


def table(name):
    want = (name + '.afs').encode()
    i = db.find(want + b'\0', 16)
    at = i - 16
    hdr = struct.unpack('<4I', db[at:at + 16])
    assert hdr[0] == 0 and hdr[3] == hdr[2] and hdr[1] == 0x11A + hdr[2] * 2, hdr
    sectors = struct.unpack('<%dH' % hdr[2], db[at + 0x11A:at + 0x11A + hdr[2] * 2])
    offs = []
    off = 0
    for s in sectors:
        offs.append((off, s * SEC))
        off += s * SEC
    return offs


def member(name, n):
    offs = table(name)
    loc, ln = datad[name.upper() + '.AFS']
    off, size = offs[n]
    size = min(size, ln - off)
    return rd(loc, 0)[:0] + (lambda: (iso.seek(loc * SEC + off), iso.read(size))[1])()


def bpe(src):
    size, psz = struct.unpack('<II', src[:8])
    p = 8
    res = bytearray()
    while p - 8 != psz:
        left = list(range(256))
        right = [0] * 256
        count = src[p]; p += 1
        c = 0
        while True:
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            for _ in range(count + 1):
                left[c] = src[p]; p += 1
                if c != left[c]:
                    right[c] = src[p]; p += 1
                c += 1
            if c == 256:
                break
            count = src[p]; p += 1
        bs = (src[p] << 8) + src[p + 1]; p += 2
        stack = []
        while True:
            if stack:
                cc = stack.pop()
            else:
                if bs == 0:
                    break
                bs -= 1
                cc = src[p]; p += 1
            if cc == left[cc]:
                res.append(cc)
            else:
                stack.append(right[cc]); stack.append(left[cc])
    assert len(res) == size, (len(res), size)
    return bytes(res)


def section(pack, n):
    w = struct.unpack('<I', pack[n * 4:n * 4 + 4])[0] & ~3
    cnt = struct.unpack('<I', pack[:4])[0]
    e = (struct.unpack('<I', pack[(n + 1) * 4:(n + 1) * 4 + 4])[0] & ~3) if n < cnt else len(pack)
    return pack[w:e]


# the options screen of BT4 B14 Rev2 (English and Spanish): pzs4us1 member 453, its section 1 unpacked (38 sections)
s1 = bpe(section(member('pzs4us1', 453), 1))
open(os.path.join(out, 'opt_s1.bin'), 'wb').write(s1)
print('opt_s1.bin', len(s1), 'sections', struct.unpack('<I', s1[:4])[0])
