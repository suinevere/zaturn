import gzip,re
LAB={1:"UP",2:"DOWN",3:"LEFT",4:"RIGHT",5:"L-tr",6:"R-tr"}
for n in range(1,7):
    raw=gzip.open(f"mednafen-1.32.1-win64/mcs/Steamgear Mash (Japan).aec07ed27fca99c73730cd6603bd58b8.mc{n}",'rb').read()
    if n==1:
        # list field names: mednafen uses [len][name][4-byte size]... find printable tokens
        toks=sorted(set(re.findall(rb'[A-Za-z][A-Za-z0-9_]{2,15}',raw)))
        sm=[t.decode() for t in toks if b'SMPC' in t or b'OREG' in t or b'IREG' in t or b'PORT' in t or b'pad' in t.lower() or b'Peri' in t or b'IO' in t]
        print("fields of interest:", sm)
    # find OREG-ish: search for the field 'SMPC' and dump bytes after
    for key in (b"OREG",b"SMPC",b"PadData",b"IOPort"):
        i=raw.find(key)
        if i>=0:
            seg=raw[i-1:i+40]
            print(f" mc{n} {LAB[n]:5} [{key.decode()}@{i}]: "+" ".join(f"{b:02x}" for b in seg))
            break
