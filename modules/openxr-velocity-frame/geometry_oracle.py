"""Firmware descriptor constants and independent host quaternion math."""
import math,struct
def mul(a,b):
    x,y,z,w=a; X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)
def conj(q): return (-q[0],-q[1],-q[2],q[3])
def rotate(q,v): return mul(mul(q,(*v,0)),conj(q))[:3]
def f(bits): return struct.unpack('<f',struct.pack('<I',bits))[0]
# addSupportedDevice 0x15fc4..0x161e8: embedded rotation matrices and translations.
GRIP=math.atan2(f(0x3f2c6c61),f(0x3f3d39a9))
AIM=-math.atan2(f(0x3e8483ee),f(0x3f7746ea))
def descriptor(name,device):
    return {
        0x130005:(AIM,(0,0,f(0xbca3d70a))),
        0x140005:(GRIP,(0,0,0)),
        0x150005:(0,((-1 if device==3 else 1)*f(0x3ca3d70a),f(0xbd176154),f(0x3d09f04e))),
        0x160005:(0,(0,f(0x3c23d70a),f(0xbd23d70a))),
        0x170005:(0,(0,f(0x3c23d70a),f(0xbd4ccccd))),
        0x180005:(GRIP,(0,f(0xbd6fc945),f(0x3d5a7eb0))),
    }[name]
def pitch(a): return (math.sin(a/2),0,0,math.cos(a/2))
