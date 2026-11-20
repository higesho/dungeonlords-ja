# Noto Serif JP (OFL) -> SemiBold static instance, subset to the characters the translation uses
# -> build/dljp_font.ttf (embedded in the DLL as resource DLJP/2)
import json, os, sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SRC = r'C:\Windows\Fonts\NotoSerifJP-VF.ttf'
tr = json.load(open(os.path.join(ROOT, 'build', 'tr.json'), encoding='utf-8'))
chars = set()
for v in tr.values(): chars.update(v)
# basic kana / punctuation / full-width forms that the player may meet (names etc.)
for a, b in ((0x3000, 0x30ff), (0xff01, 0xff5e), (0x2010, 0x2027), (0x2190, 0x2193), (0x25a0, 0x25cf)):
    chars.update(chr(c) for c in range(a, b + 1))
chars = {c for c in chars if ord(c) > 0x7f}
print('chars', len(chars))
f = TTFont(SRC)
f = instancer.instantiateVariableFont(f, {'wght': 600})
opt = subset.Options()
opt.name_IDs = ['*']; opt.name_languages = ['*']; opt.notdef_outline = True; opt.hinting = False
opt.layout_features = ['vert', 'vrt2', 'palt'] ; opt.drop_tables += ['DSIG']
s = subset.Subsetter(opt)
s.populate(unicodes=[ord(c) for c in chars])
s.subset(f)
# unique family name so it never clashes with an installed Noto Serif JP
for rec in f['name'].names:
    if rec.nameID in (1, 4, 16, 6):
        rec.string = 'DLJP Serif' if rec.nameID != 6 else 'DLJPSerif'
f.save(os.path.join(ROOT, 'build', 'dljp_font.ttf'))
print(os.path.getsize(os.path.join(ROOT, 'build', 'dljp_font.ttf')), 'bytes')
