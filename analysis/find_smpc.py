import struct
b=open("analysis/_orig0.BIN","rb").read() if __import__('os').path.exists("analysis/_orig0.BIN") else open("game_originals/Steamgear Mash (Japan)/0.BIN","rb").read()
LOAD=0x06004000
def find_be32(val):
    needle=struct.pack(">I",val); out=[]; i=0
    while True:
        i=b.find(needle,i)
        if i<0: break
        out.append(i); i+=1
    return out
for name,val in [("SMPC base 0x20100000",0x20100000),("OREG0 0x20100021",0x20100021),
                 ("OREG base 0x20100020",0x20100020),("0x20100001",0x20100001),
                 ("padbuf 0x060EFBE0",0x060EFBE0),("padbuf 0x060EFBDC",0x060EFBDC),
                 ("padbuf 0x060EFBE1",0x060EFBE1),("0x060EFBFC",0x060EFBFC),
                 ("0x060EFB50",0x060EFB50),("COMREG 0x20100000 area",0x2010001f)]:
    locs=find_be32(val)
    print(f"{name}: {[hex(LOAD+x)+f'(file 0x{x:X})' for x in locs[:8]]}")
