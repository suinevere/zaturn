import gzip
LABELS={1:"UP   ",2:"DOWN ",3:"LEFT ",4:"RIGHT",5:"L-tr ",6:"R-tr "}
def load(path,field=b"WorkRAMH",size=0x100000):
    raw=gzip.open(path,'rb').read(); i=raw.find(field); start=i-1; off=start+13
    blk=raw[off:off+size]; b=bytearray(size)
    for j in range(0,size-1,2): b[j]=blk[j+1]; b[j+1]=blk[j]
    return bytes(b)
dumps={n:load(f"mednafen-1.32.1-win64/mcs/Steamgear Mash (Japan).aec07ed27fca99c73730cd6603bd58b8.mc{n}") for n in range(1,7)}
# dump region around the pad mirrors
for label,lo,hi in [("rec @EFBD8",0xEFBD8,0xEFC10)]:
    print(f"== {label} ==")
    for n in range(1,7):
        d=dumps[n]
        print(f" {LABELS[n]}: "+" ".join(f"{d[o]:02x}" for o in range(lo,hi)))
    print("  addr: "+" ".join(f"{(0x06000000+o)&0xff:02x}" for o in range(lo,hi)))
