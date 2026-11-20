import sys, pefile, capstone, struct
EXE = r"L:\SteamLibrary\steamapps\common\Dungeon Lords\DLSteamEdition.exe"
pe = pefile.PE(EXE, fast_load=True)
data = open(EXE,'rb').read()
base = pe.OPTIONAL_HEADER.ImageBase
def off2va(o):
    for s in pe.sections:
        if s.PointerToRawData <= o < s.PointerToRawData+s.SizeOfRawData:
            return base+s.VirtualAddress+o-s.PointerToRawData
def va2off(v):
    r=v-base
    for s in pe.sections:
        if s.VirtualAddress<=r<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            return s.PointerToRawData+r-s.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
def dis(va, n=60):
    o=va2off(va)
    for i in md.disasm(data[o:o+n*8], va):
        print(f"{i.address:08x}: {i.mnemonic} {i.op_str}")
        n-=1
        if n<=0: break
def xrefs(va):
    """find push imm32 / mov reg,imm32 references to va in code"""
    pat=struct.pack('<I',va); res=[]
    i=data.find(pat)
    while i!=-1:
        res.append(off2va(i)); i=data.find(pat,i+1)
    return res
def funcstart(va):
    o=va2off(va)
    for k in range(o, o-0x4000, -1):
        if data[k-1] in (0xcc,0xc3) and data[k]==0x55 and data[k+1:k+3]==b'\x8b\xec': return off2va(k)
        if data[k-1]==0xcc and data[k]!=0xcc: return off2va(k)
if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='x':
        for a in sys.argv[2:]:
            v=int(a,16); v = v if v>base else off2va(v)
            print(hex(v),[hex(x) for x in xrefs(v)])
    elif cmd=='d': dis(int(sys.argv[2],16), int(sys.argv[3]) if len(sys.argv)>3 else 60)
    elif cmd=='f': print(hex(funcstart(int(sys.argv[2],16))))
