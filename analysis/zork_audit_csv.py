import re, glob, sys, csv
sys.path.insert(0, 'analysis')
from zork_data.rooms import ROOMS

# index -> (kind, ZIL name)
MAP = {
 1:('room','FOREST-2'),2:('room','MAZE-1'),3:('room','DEAD-END-1'),4:('room','MACHINE-ROOM'),
 5:('room','WINDING-PASSAGE'),10:('room','CLEARING'),11:('room','FOREST-1'),12:('room','MOUNTAINS'),
 13:('room','NORTH-OF-HOUSE'),14:('room','PATH'),15:('room','SOUTH-OF-HOUSE'),16:('room','STONE-BARROW'),
 19:('room','ATTIC'),22:('room','EAST-OF-CHASM'),23:('room','GALLERY'),24:('room','STUDIO'),
 25:('room','TROLL-ROOM'),30:('room','CYCLOPS-ROOM'),31:('room','TREASURE-ROOM'),33:('room','IN-STREAM'),
 34:('room','STREAM-VIEW'),36:('room','ATLANTIS-ROOM'),37:('room','COLD-PASSAGE'),38:('room','NARROW-PASSAGE'),
 39:('room','SMALL-CAVE'),40:('room','TINY-CAVE'),41:('room','CHASM-ROOM'),42:('room','DAMP-CAVE'),
 43:('room','EW-PASSAGE'),44:('room','NS-PASSAGE'),45:('room','ROUND-ROOM'),48:('room','ENTRANCE-TO-HADES'),
 49:('room','EGYPT-ROOM'),50:('room','ENGRAVINGS-CAVE'),51:('room','NORTH-TEMPLE'),52:('room','SOUTH-TEMPLE'),
 56:('room','DAM-LOBBY'),57:('room','MAINTENANCE-ROOM'),60:('room','CANYON-BOTTOM'),61:('room','CANYON-VIEW'),
 62:('room','CLIFF-MIDDLE'),63:('room','DAM-BASE'),64:('room','END-OF-RAINBOW'),65:('room','ARAGAIN-FALLS'),
 66:('room','RIVER-1'),67:('room','RIVER-2'),68:('room','RIVER-3'),69:('room','RIVER-4'),70:('room','RIVER-5'),
 71:('room','SANDY-BEACH'),72:('room','SANDY-CAVE'),73:('room','SHORE'),74:('room','WHITE-CLIFFS-SOUTH'),
 75:('room','WHITE-CLIFFS-NORTH'),77:('room','DEAD-END-5'),78:('room','GAS-ROOM'),79:('room','LADDER-BOTTOM'),
 80:('room','LADDER-TOP'),81:('room','LOWER-SHAFT'),82:('room','MINE-ENTRANCE'),83:('room','SHAFT-ROOM'),
 84:('room','SMELLY-ROOM'),85:('room','SQUEEKY-ROOM'),86:('room','TIMBER-ROOM'),90:('room','SLIDE-ROOM'),
 7:('obj','BROKEN-EGG'),8:('obj','DIAMOND'),9:('obj','HOT-BELL'),17:('obj','LEAFLET'),18:('obj','LEAVES'),
 26:('obj','PAINTING'),27:('obj','TROLL'),28:('obj','SKELETON'),29:('obj','BAG-OF-COINS'),32:('obj','CHALICE'),
 35:('obj','TRUNK'),46:('obj','BAR'),47:('obj','THIEF'),53:('obj','COFFIN'),54:('obj','ENGRAVINGS'),
 55:('obj','SCEPTRE'),58:('obj','MATCHBOOK'),59:('obj','TUBE'),76:('obj','INFLATABLE-BOAT'),87:('obj','JADE'),
 92:('obj','THIEF'),
}

# --- pull ZIL LDESC and FDESC for ROOM and OBJECT defs ---
def grab(s,i):
    o=[]
    while i<len(s):
        c=s[i]
        if c=='\\': o.append(s[i+1]); i+=2; continue
        if c=='"': break
        o.append(c); i+=1
    return ''.join(o)
zil={}  # name -> {'LDESC':..,'FDESC':..}
for f in glob.glob('cd/Zork I - The Great Underground Empire (Japan)/zork1/*.zil'):
    z=open(f,encoding='latin-1').read()
    for m in re.finditer(r'<(ROOM|OBJECT) (\S+)', z):
        name=m.group(2); s=m.end()
        nxt=re.search(r'\n<[A-Z]', z[s:]); blk=z[s:s+(nxt.start() if nxt else 2000)]
        d=zil.setdefault(name,{})
        for prop in ('LDESC','FDESC'):
            k=blk.find('('+prop)
            if k>=0:
                q=blk.find('"',k)
                if q>=0: d[prop]=re.sub(r'\s+',' ',grab(blk,q+1)).strip()

# --- decoded JP per index from the dump (cleaned at overrun markers) ---
jp={}
for L in open('analysis/zork_strings_dump.txt',encoding='utf-8'):
    m=re.match(r'rooms98\s+\[\s*(\d+)\]\s+0x[0-9a-f]+\s+(.*)',L)
    if not m: continue
    t=m.group(2)
    for cut in ['{16}','がここに{','{06}{09}','<0e:fe>','{1d}{1f}']:
        k=t.find(cut)
        if k>0: t=t[:k]
    jp[int(m.group(1))]=t.strip()

rows=[]
for ix in sorted(ROOMS):
    kind,name=MAP.get(ix,('?','?'))
    info=zil.get(name,{})
    inform=info.get('LDESC') or info.get('FDESC') or '(no static desc - auto-generated)'
    rows.append([ix,kind,name,jp.get(ix,''),ROOMS[ix],inform])

with open('analysis/zork_translation_audit.csv','w',newline='',encoding='utf-8-sig') as fh:
    w=csv.writer(fh)
    w.writerow(['index','kind','zil_name','japanese_source','my_english','infocom_zil'])
    w.writerows(rows)
print('wrote analysis/zork_translation_audit.csv :',len(rows),'rows')
