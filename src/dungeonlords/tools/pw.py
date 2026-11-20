# PrintWindow capture of the game window found by PID + title "Dungeon Lords"
import sys, ctypes, ctypes.wintypes as W, os
from PIL import Image
u=ctypes.windll.user32; g=ctypes.windll.gdi32
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'..'))
pid=int(open(os.path.join(ROOT,'work','pid.txt')).read())
hw=[]
@ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
def cb(h,l):
    p=W.DWORD(); u.GetWindowThreadProcessId(h,ctypes.byref(p))
    b=ctypes.create_unicode_buffer(64); u.GetWindowTextW(h,b,64)
    if p.value==pid and b.value=='Dungeon Lords': hw.append(h)
    return True
u.EnumWindows(cb,0)
h=hw[0]; r=W.RECT(); u.GetClientRect(h,ctypes.byref(r)); w,hh=r.right,r.bottom
dc=u.GetDC(h); md=g.CreateCompatibleDC(dc); bm=g.CreateCompatibleBitmap(dc,w,hh); g.SelectObject(md,bm)
u.PrintWindow(h,md,3)
buf=ctypes.create_string_buffer(w*hh*4)
class BIH(ctypes.Structure):
    _fields_=[('a',W.DWORD),('w',W.LONG),('h',W.LONG),('p',W.WORD),('b',W.WORD),('c',W.DWORD),('s',W.DWORD),('x',W.LONG),('y',W.LONG),('u',W.DWORD),('i',W.DWORD)]
bi=BIH(40,w,-hh,1,32,0,0,0,0,0,0)
g.GetDIBits(md,bm,0,hh,buf,ctypes.byref(bi),0)
im=Image.frombuffer('RGBA',(w,hh),buf,'raw','BGRA',0,1).convert('RGB')
im.resize((w//2,hh//2)).save(os.path.join(ROOT,'capture',sys.argv[1]+'.png'))
