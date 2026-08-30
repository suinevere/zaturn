#!/usr/bin/env python3
"""msg777 translation audit: decode each chosen index (bounded) + my English draft -> CSV.

Drafts are faithful to the Saturn JP wording (option A), aligned to Infocom phrasing where the
message is a known Zork string. [WORD] / {ROOM} mark where 0x0e object / 0x1e room tokens stay.
Once reviewed, these become the MSGS map (zork_data/messages.py) for the build."""
import sys, csv
sys.path.insert(0, 'analysis')
import zork_msg_decode as M

EN = {
 # --- travel-blocked / action-failure replies ---
 0:   "THE [GRATING] IS CLOSED! YOU CAN'T GO THAT WAY.",
 33:  "YOU CANNOT CLIMB ANY HIGHER.",
 36:  "THE [DOOR] IS BOARDED, AND YOU CAN'T REMOVE THE [BOARDS].",
 42:  "ONLY SANTA CLAUS CAN GO DOWN THE [CHIMNEY].",
 45:  "THE [DOOR] IS NAILED SHUT.",
 48:  "YOU TRY TO SCRAMBLE UP THE [SLIDE], BUT FAIL, AND SLIDE BACK DOWN.",
 51:  "THE [CHASM] PROBABLY LEADS DOWN INTO HELL.",
 135: "THE [DAM] BLOCKS YOUR WAY.",
 144: "THE STREAM ISSUES FROM BETWEEN SMALL ROCKS; YOU CAN'T SQUEEZE IN.",
 174: "HAVE YOU LOST YOUR MIND?",
 177: "IF YOU WERE AN INSECT, YOU MIGHT JUST SQUEEZE THROUGH.",
 201: "UNLESS YOU FANCY A BROKEN BONE, YOU CAN'T GET DOWN THERE.",
 213: "THE HOLE IS TOO SMALL TO CARRY THE COFFIN DOWN.",
 228: "A LONG WAY DOWN...",
 303: "IT WOULD BE HARD TO GET IN THERE. IF YOU DID, YOU WOULD FALL TO YOUR DEATH.",
 336: "THE [GROUND] HERE IS TOO HARD TO DIG.",
 360: "YOU MUST SAY WHICH WAY TO GO. I CAN'T HELP YOU WITH THAT.",
 423: "SAY WHETHER YOU WANT TO GO UP OR DOWN.",
 468: "THERE IS NO SUCH THING HERE.",
 # --- object 'X is here' / scenery descriptions ---
 462: "A RED HOT [BELL].",
 505: "ON A BRANCH BESIDE YOU IS A SMALL [BIRD'S NEST].",
 511: "A BOTTLE IS SITTING ON THE [TABLE].",
 523: "A BATTERY-POWERED [LAMP] IS ON THE [TROPHY CASE].",
 529: "IN THE [TROPHY CASE] IS A PARCHMENT WHICH APPEARS TO BE A MAP.",
 532: "IN ONE CORNER IS A LARGE COIL OF [ROPE].",
 538: "ON THE [TABLE] IS A LONG BROWN SACK, SMELLING OF HOT PEPPERS.",
 541: "ABOVE THE [TROPHY CASE] HANGS A VERY ANCIENT [ELVISH SWORD].",
 559: "LOOSELY ATTACHED TO THE WALL IS A PAPER [LEAFLET].",
 574: "THE DECEASED ADVENTURER'S USELESS, BURNED-OUT [LAMP] IS HERE.",
 580: "BESIDE THE [SKELETON] IS A [RUSTY KNIFE].",
 592: "HALF BURIED IN THE MUD IS AN [OLD TRUNK], BULGING WITH [JEWELS].",
 601: "ON THE [SHORE] LIES POSEIDON'S OWN [TRIDENT].",
}

if __name__ == "__main__":
    a = M._load()
    rows = [[ix, M.decode(a, None, M.MSG_TBL, ix), EN[ix]] for ix in sorted(EN)]
    with open('analysis/zork_msg777_audit.csv', 'w', newline='', encoding='utf-8-sig') as fh:
        w = csv.writer(fh); w.writerow(['msg777_index', 'japanese_decoded', 'my_english_draft']); w.writerows(rows)
    print('wrote analysis/zork_msg777_audit.csv :', len(rows), 'rows')
