import gzip
LAB={1:"U",2:"D",3:"L",4:"R",5:"l",6:"r"}
def load(path,field,size):
    raw=gzip.open(path,'rb').read(); i=raw.find(field)
    if i<0: return None
    off=i-1+13; blk=raw[off:off+size]; b=bytearray(size)
    for j in range(0,size-1,2): b[j]=blk[j+1]; b[j+1]=blk[j]
    return bytes(b)
def get(field,size):
    return {n:load(f"mednafen-1.32.1-win64/mcs/Steamgear Mash (Japan).aec07ed27fca99c73730cd6603bd58b8.mc{n}",field,size) for n in range(1,7)}

for field,base,size,name in [(b"WorkRAML",0x00200000,0x100000,"LWRAM"),(b"WorkRAMH",0x06000000,0x100000,"HWRAM")]:
    d=get(field,size)
    if d[1] is None: print(name,"missing"); continue
    # analog X signature: LEFT(3) low, RIGHT(4) high, U/D(1,2) mid; analog Y: UP(1) low, DOWN(2) high
    hitsX=[]; hitsY=[]
    for o in range(size):
        v=[d[n][o] for n in range(1,7)]
        # require values not all ff/00, spread>=0x30
        if max(v)-min(v) < 0x30: continue
        # X axis: v[2](LEFT)<v[3](RIGHT) clearly, and l/r-trig(v[4],v[5]) near center
        if v[2] < v[3]-0x30 and abs(v[0]-0x80)<0x50 and abs(v[1]-0x80)<0x50:
            hitsX.append((o,v))
        if v[0] < v[1]-0x30 and abs(v[2]-0x80)<0x50 and abs(v[3]-0x80)<0x50:
            hitsY.append((o,v))
    print(f"=== {name}: X-axis candidates={len(hitsX)} Y-axis candidates={len(hitsY)} ===")
    for o,v in hitsX[:15]: print(f"  X? 0x{base+o:08X}: "+" ".join(f"{x:02x}" for x in v))
    for o,v in hitsY[:15]: print(f"  Y? 0x{base+o:08X}: "+" ".join(f"{x:02x}" for x in v))
