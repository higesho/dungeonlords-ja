# Merge translated chunks into build/tr.json  {English game string: Japanese}
#  - paragraph units (UI) are split back into their original number of lines
#  - topic translations are applied to every letter-case variant
#  - text/fixes.json ({English: Japanese}) overrides everything
import os, json, re, glob, unicodedata
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
CH = os.path.join(ROOT, 'text', 'chunks')
JA = os.path.join(ROOT, 'text', 'ja')

NO_START = set('、。，．・：；？！ー）」』】〕｝〉》ゃゅょっぁぃぅぇぉャュョッァィゥェォ…～')
NO_END = set('「『（【〔')

def cw(c):
    return 2 if unicodedata.east_asian_width(c) in 'WF' else 1

def split_lines(s, n):
    """split Japanese s into n lines of similar display width"""
    s = s.strip()
    if n <= 1: return [s]
    total = sum(cw(c) for c in s)
    target = total / n
    lines, cur, acc = [], '', 0
    for i, c in enumerate(s):
        nxt = s[i + 1] if i + 1 < len(s) else ''
        cur += c; acc += cw(c)
        if len(lines) < n - 1 and nxt and acc >= target * (len(lines) + 1):
            if nxt in NO_START or c in NO_END: continue
            if c.isascii() and c.isalnum() and nxt.isascii() and nxt.isalnum(): continue
            lines.append(cur.strip()); cur = ''
    lines.append(cur.strip())
    while len(lines) < n: lines.append('')
    return lines

def load_pairs():
    pairs = []   # (unit, ja)
    for fn in sorted(glob.glob(os.path.join(JA, '*.json'))):
        name = os.path.basename(fn)[:-5]
        cp = os.path.join(CH, name + '.json')
        if not os.path.exists(cp): continue
        try:
            ja = {u['id']: u['ja'] for u in json.load(open(fn, encoding='utf-8')) if isinstance(u, dict) and 'id' in u and 'ja' in u}
        except Exception as e:
            print('skip', name, e); continue
        for u in json.load(open(cp, encoding='utf-8')):
            if u['id'] in ja and isinstance(ja[u['id']], str): pairs.append((name, u, ja[u['id']]))
    return pairs

def clean(j):
    j = j.replace('～', '〜')
    return j

def main():
    tr = {}
    pairs = load_pairs()
    topics = {}
    for name, u, j in pairs:
        j = clean(j)
        if 'members' in u:
            parts = split_lines(j, u['lines'])
            for en, ja in zip(u['members'], parts):
                lead = len(en) - len(en.lstrip(' '))
                tr.setdefault(en, ' ' * min(lead, 3) + ja)
            continue
        if name.startswith('topics'): topics[u['en'].lower()] = j
        tr.setdefault(u['en'], j)
    # UI strings with leading/trailing blanks were translated without them
    for e in json.load(open(os.path.join(ROOT, 'text', 'src', 'ui.json'), encoding='utf-8')):
        o = e['en']
        if o in tr or o.strip() not in tr or not o.strip(): continue
        lead = o[:len(o) - len(o.lstrip(' '))]
        trail = o[len(o.rstrip(' ')):]
        tr[o] = lead + tr[o.strip()] + trail
    # every case variant of a topic
    src = os.path.join(ROOT, 'text', 'src', 'topics.json')
    tf = os.path.join(ROOT, 'text', 'topic_fixes.json')
    if os.path.exists(tf):
        for k, v in json.load(open(tf, encoding='utf-8')).items(): topics[k.lower()] = v
    for e in json.load(open(src, encoding='utf-8')):
        if e['en'].lower() in topics: tr[e['en']] = topics[e['en'].lower()]
    for en in list(tr):
        if en.lower() in topics: tr[en] = topics[en.lower()]
    rep = os.path.join(ROOT, 'text', 'replace.json')
    if os.path.exists(rep):
        rp = json.load(open(rep, encoding='utf-8'))
        for k in tr:
            for a, b in rp.items():
                if a in tr[k] and a not in k.replace(a, '') or (a in tr[k] and k.upper() != k): tr[k] = tr[k].replace(a, b)
    fix = os.path.join(ROOT, 'text', 'fixes.json')
    if os.path.exists(fix):
        tr.update(json.load(open(fix, encoding='utf-8')))
    # the game keeps monster/prop names in 21-byte fields and item names in 31-byte fields
    def ebytes(t):
        return len(t.replace('〜', '～').encode('euc_jp', 'replace'))
    for g, lim in (('monsters', 20), ('props', 20), ('items', 30)):
        for e in json.load(open(os.path.join(ROOT, 'text', 'src', g + '.json'), encoding='utf-8')):
            k = e['en']
            if k in tr and ebytes(tr[k]) > lim:
                t = tr[k]
                while ebytes(t) > lim: t = t[:-1]
                print('  too long for the game (%s): %s -> %s' % (g, tr[k], t))
                tr[k] = t
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    json.dump(tr, open(os.path.join(ROOT, 'build', 'tr.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print('merged', len(tr), 'strings from', len(pairs), 'units')

if __name__ == '__main__':
    main()
