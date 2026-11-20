# Extract every translatable string from the game's text files into text/src/*.json
# (unique English strings, in reading order, with a little context for the translators).
import sys, os, json, re
sys.path.insert(0, os.path.dirname(__file__))
from dlfmt import *

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'text', 'src')

def dec(b): return b.decode('latin-1')

def skip(s):
    if not s.strip(): return True
    if not re.search(r'[A-Za-z]', s): return True
    return False

def main():
    os.makedirs(OUT, exist_ok=True)
    seen = set()
    groups = {}
    def add(group, s, ctx):
        if skip(s) or s in seen: return
        seen.add(s)
        groups.setdefault(group, []).append({'en': s, 'ctx': ctx})
    pak = read_pak(gp('NPCDATA.PAK'))
    # topics first: they define keyword translations used inside dialog ~markers~
    for k, s in keytable(pak[0]): add('topics', dec(s), 'topic')
    for f, g in (('D6STRING.DAT', 'ui'), ('INAME.DAT', 'items'), ('MNAME.DAT', 'monsters'), ('PNAME.DAT', 'props')):
        for i, s in read_idtable(gp(f)): add(g, dec(s), '%s:%d' % (f.split('.')[0], i))
    for i, s in read_quest(gp('QUESTTEXT.PAK')): add('quest', dec(s.rstrip(b'\0')), 'quest:%d' % i)
    for n, b in enumerate(pak):
        t = strtable(b)
        if not t: continue
        for k, s in enumerate(t): add('npc', dec(s), 'npc%d:%d' % (n, k))
    # DLC pak (not referenced by the exe, but harmless)
    for n, b in enumerate(read_pak(gp('DLCEV15_NPCDATA.PAK'))):
        t = strtable(b)
        if t:
            for k, s in enumerate(t): add('npc', dec(s), 'dlc%d:%d' % (n, k))
    tot = 0
    for g, L in groups.items():
        w = sum(len(e['en'].split()) for e in L)
        tot += w
        print(g, len(L), 'strings', w, 'words')
        json.dump(L, open(os.path.join(OUT, g + '.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('total words', tot)

if __name__ == '__main__':
    main()
