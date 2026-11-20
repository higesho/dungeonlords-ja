# Find string literals in DLSteamEdition.exe that the code references (push/mov imm32 or pointer tables).
# Output: work/exe_strings.json  [{va, s, refs:[{at, kind, next_call}]}]
import sys, os, re, json, struct
sys.path.insert(0, os.path.dirname(__file__))
from dlx import data, base, off2va, va2off
import capstone

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
TEXT = (0x1000, 0x1000 + 0x2c10c1)
RDATA = (0x2c3000, 0x2c3000 + 0x8c070)
DATA_RAW = (0x350000, 0x350000 + 0x7a000)   # raw part of .data in the file

def strings():
    out = {}
    for lo, hi in (RDATA, DATA_RAW):
        for m in re.finditer(rb'[\x09\x20-\x7e]{2,}\x00', data[lo:hi]):
            o = lo + m.start()
            out[off2va(o)] = m.group()[:-1]
            # also substrings referenced at an offset are rare; ignore
    return out

def refs(strs):
    vas = set(strs)
    res = {}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = False
    # code: scan every 4-byte window equal to a string VA, then confirm it is an imm32 operand
    for lo, hi, kind in ((TEXT[0], TEXT[1], 'code'), (RDATA[0], RDATA[1], 'rdata'), (DATA_RAW[0], DATA_RAW[1], 'data')):
        buf = data[lo:hi]
        for i in range(0, len(buf) - 3):
            v = struct.unpack_from('<I', buf, i)[0]
            if v not in vas: continue
            at = off2va(lo + i)
            if kind == 'code':
                op = buf[i - 1] if i else 0
                k = None
                if op == 0x68: k = 'push'
                elif 0xb8 <= op <= 0xbf: k = 'mov'
                elif i >= 2 and buf[i - 2] == 0xc7: k = 'movm'
                elif i >= 3 and buf[i - 3] == 0xc7: k = 'movm'
                elif i >= 4 and buf[i - 4] == 0xc7: k = 'movm'
                elif i >= 6 and buf[i - 6] == 0xc7 and buf[i - 5] in (0x84, 0x44): k = 'movm'
                if not k: continue
                # what is called right after (log function = debug text)
                nxt = None
                for ins in md.disasm(buf[i + 4:i + 4 + 64], at + 4):
                    if ins.mnemonic == 'call':
                        nxt = ins.op_str; break
                    if ins.mnemonic in ('ret', 'jmp'): break
                res.setdefault(v, []).append({'at': at, 'kind': k, 'call': nxt})
            else:
                if i % 4: continue
                res.setdefault(v, []).append({'at': at, 'kind': kind, 'call': None})
    return res

def main():
    strs = strings()
    rf = refs(strs)
    out = []
    for va in sorted(rf):
        s = strs[va]
        out.append({'va': va, 's': s.decode('latin-1'), 'refs': rf[va]})
    os.makedirs(os.path.join(ROOT, 'work'), exist_ok=True)
    json.dump(out, open(os.path.join(ROOT, 'work', 'exe_strings.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print(len(out), 'referenced strings')

if __name__ == '__main__':
    main()
