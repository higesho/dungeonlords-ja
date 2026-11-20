# Dungeon Lords text container formats
import struct
GAME = r'L:\SteamLibrary\steamapps\common\Dungeon Lords'
def read_idtable(path):
    """D6STRING / INAME / MNAME / PNAME: count,(id,off)*,strings"""
    d=open(path,'rb').read()
    n=struct.unpack_from('<I',d,0)[0]
    out=[]
    for i in range(n):
        idx,off=struct.unpack_from('<II',d,4+i*8)
        e=d.find(b'\0',off); out.append((idx,d[off:e]))
    return out
def read_pak(path):
    d=open(path,'rb').read()
    n=struct.unpack_from('<I',d,0)[0]//8
    return [d[o:o+s] for o,s in (struct.unpack_from('<II',d,i*8) for i in range(n))]
def strtable(b):
    if len(b)<8: return None
    c=struct.unpack_from('<I',b,0)[0]
    if not (0<c<2000 and 4+4*c<=len(b) and struct.unpack_from('<I',b,4)[0]==4+4*c and b[-1]==0): return None
    offs=struct.unpack_from('<%dI'%c,b,4)
    if not all(b[x-1]==0 for x in offs[1:]): return None
    return [b[o:b.find(b'\0',o)] for o in offs]
def keytable(b):
    c=struct.unpack_from('<I',b,0)[0]
    out=[]
    for i in range(c):
        o,k=struct.unpack_from('<IB',b,4+i*5)
        out.append((k,b[o:b.find(b'\0',o)]))
    return out
def read_quest(path):
    d=open(path,'rb').read()
    n=struct.unpack_from('<I',d,0)[0]
    return [(lambda i,o,l:(i,d[o:o+l]))(*struct.unpack_from('<III',d,4+k*12)) for k in range(n)]
import os
def gp(name): return os.path.join(GAME, name)
