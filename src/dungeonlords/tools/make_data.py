# text/ja/*.json ({English: Japanese}) -> build/dljp.bin (resource DLJP/1 of dinput8.dll)
import json, glob, os, struct, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
def fnv64(b):
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c; h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h
def to_game(s):
    """Japanese text -> game bytes (EUC-JP). Latin stays ASCII."""
    for a, b in (('～', '〜'), ('－', '−'), ('—', '―'), ('∥', '‖')):
        s = s.replace(a, b)
    out = bytearray()
    for ch in s:
        try:
            out += ch.encode('euc_jp')
        except UnicodeEncodeError:
            print('  unencodable', repr(ch), 'in', s[:40], file=sys.stderr)
            out += b'?'
    return bytes(out)
def load_all():
    return json.load(open(os.path.join(ROOT, 'build', 'tr.json'), encoding='utf-8'))
def main():
    tr = load_all()
    ents = {}
    pool = bytearray()
    for en, ja in tr.items():
        enb = en.encode('latin-1')
        h = fnv64(enb)
        ents[h] = to_game(ja)
    keys = sorted(ents)
    head = 8 + 12 * len(keys)
    tab = bytearray(); 
    for k in keys:
        tab += struct.pack('<III', k & 0xffffffff, k >> 32, head + len(pool))
        pool += ents[k] + b'\0'
    data = b'DLJ1' + struct.pack('<I', len(keys)) + tab + pool
    open(os.path.join(ROOT, 'build', 'dljp.bin'), 'wb').write(data)
    print('entries', len(keys), 'bytes', len(data))
def exe_table():
    """text/exe_ja.json -> build/dljp_exe.bin: (va, len, fnv64 of the English) + Japanese"""
    sys.path.insert(0, os.path.dirname(__file__))
    from dlx import data, off2va
    tr = json.load(open(os.path.join(ROOT, 'text', 'exe_ja.json'), encoding='utf-8'))
    ents, pool = [], bytearray()
    for en, ja in tr.items():
        b = en.encode('latin-1') + bytes(1)
        o = data.find(b)
        while o > 0 and data[o - 1] != 0: o = data.find(b, o + 1)
        if o < 0: print('exe string not found:', en); continue
        h = fnv64(b[:-1])
        ents.append((off2va(o), len(b) - 1, h, len(pool)))
        pool += to_game(ja) + bytes(1)
    head = 8 + 20 * len(ents)
    out = b'DLX1' + struct.pack('<I', len(ents))
    for va, n, h, off in ents: out += struct.pack('<IIIII', va, n, h & 0xffffffff, h >> 32, head + off)
    open(os.path.join(ROOT, 'build', 'dljp_exe.bin'), 'wb').write(out + pool)
    print('exe strings', len(ents))

if __name__ == '__main__':
    main()
    exe_table()
