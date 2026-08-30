import sys, os, struct
PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, PROJ)
from saturn_translate.iso import SaturnImage
from saturn_translate import sh2

DISC = os.path.join(PROJ, "game_originals", "Jun-Classic-C-C-Rope-Club_SEGA-Saturn_JA",
                    "Junclassic C.C. & Rope Club (Japan) (2M) (Track 1).bin")
WORK = os.path.join(PROJ, "analysis", "jc_work")
os.makedirs(WORK, exist_ok=True)

img = SaturnImage.from_file(DISC)
print("LAYOUT sector_size=%d data_offset=%d" % (img.sector_size, img.data_offset))
files = [e for e in img.list_files() if not e.is_dir]
print("== FILES (%d) ==" % len(files))
for e in files:
    print("  %-28s lba=%-7d size=%d" % (e.path, e.lba, e.size))

for e in files:
    data = img.extract(e.path)
    safe = e.path.strip("/").replace("/", "_")
    with open(os.path.join(WORK, safe), "wb") as fh:
        fh.write(data)
print("EXTRACTED %d files to %s" % (len(files), WORK))
