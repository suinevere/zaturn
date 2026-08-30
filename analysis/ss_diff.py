import gzip

LABELS = {1:"UP",2:"DOWN",3:"LEFT",4:"RIGHT",5:"L",6:"R"}

def load_ram(path, field, size):
    raw = gzip.open(path,'rb').read()
    i = raw.find(field)
    if i<0: return None
    start=i-1; data_off=start+13
    blk = raw[data_off:data_off+size]
    b=bytearray(size)
    for j in range(0,size-1,2):
        b[j]=blk[j+1]; b[j+1]=blk[j]
    return bytes(b)

def scan(field, base, size, name):
    dumps={}
    for n in range(1,7):
        p=f"mednafen-1.32.1-win64/mcs/Steamgear Mash (Japan).aec07ed27fca99c73730cd6603bd58b8.mc{n}"
        dumps[n]=load_ram(p,field,size)
    if dumps[1] is None:
        print(name,"MISSING"); return
    # find varying bytes
    varying=[]
    for off in range(size):
        vals=[dumps[n][off] for n in range(1,7)]
        if len(set(vals))>1:
            varying.append((off,vals))
    print(f"=== {name} ({name}) base=0x{base:08X} size=0x{size:X}: {len(varying)} varying bytes ===")
    # cluster: print runs, skip the known ring counter region 0xEFB40-0xEFBA0
    for off,vals in varying:
        addr=base+off
        # skip obvious ring/counter
        hexv=" ".join(f"{v:02x}" for v in vals)
        print(f"  0x{addr:08X}(+{off:06X}): {hexv}   [U D L R L R]")

scan(b"WorkRAMH",0x06000000,0x100000,"HWRAM")
