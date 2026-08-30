#!/usr/bin/env python3
"""Zork I (Saturn JP) — canonical English -> Japanese-reading resolver.

Foundation for on-the-fly English->JP command translation (the menu display shim AND future
keyboard input). The resident Z-machine parser only accepts the Japanese READINGS stored in
ZVOCTBL.DAT; this module maps each typed/selected English word to ONE such reading, so the parser
stays 100% Japanese internally (see docs/ZORK1_PARSER_DICT_AUDIT.md and
memory/zork-command-box-display-parse.md).

Pipeline:  english text -> tokens -> SYNONYMS canonicalize -> CANON/DIRECTIONS -> reading string
           -> (feed the existing command path that calls the resident parser)

CANON is derived from the existing translation maps (zork_data.dict_words READING_EN side +
zork_data.verbs VERBS) intersected with the actual ZVOCTBL readings, so it never drifts. Verbs use
their canonical dictionary form (the proven verb-table reading); nouns/adj prefer the normalized
kana reading (FORCE_ID-consistent where it matters).
"""
import os, sys

sys.path.insert(0, os.path.dirname(__file__))
from zork_zvoctbl_patch import records, READING_EN, load_zvoctbl, FORCE_ID
from zork_data.verbs import VERBS


def _is_kana(s):
    return bool(s) and all(0x3040 <= ord(c) <= 0x30ff or ord(c) == 0x30fc for c in s)


def _present_readings():
    """reading -> first id, for every clean ZVOCTBL record (any POS class)."""
    out = {}
    for _o, idv, _fl, kw in records(load_zvoctbl()):
        try:
            s = kw.decode("shift_jis")
        except Exception:
            continue
        if s and idv != 0xFFFF:
            out.setdefault(s, idv)
    return out


def build_canon():
    """English word -> one ZVOCTBL reading the parser accepts (auto-derived, validated)."""
    rid = _present_readings()
    verb_forms = {}     # en -> [readings] from VERBS (canonical verb forms)
    noun_forms = {}     # en -> [readings] from READING_EN (nouns/adj)
    for kanji, en in VERBS.items():
        if kanji in rid:
            verb_forms.setdefault(en, []).append(kanji)
    for reading, en in READING_EN.items():
        if reading in rid:
            noun_forms.setdefault(en, []).append(reading)

    canon = {}
    for en in set(verb_forms) | set(noun_forms):
        vf = verb_forms.get(en, [])
        if vf:                                   # verb: prefer kana dict-form, else first
            kana = [r for r in vf if _is_kana(r)]
            canon[en] = (kana or vf)[0]
            continue
        nf = list(dict.fromkeys(noun_forms.get(en, [])))
        if en in FORCE_ID:                       # honor the savestate-confirmed id
            forced = [r for r in nf if rid[r] == FORCE_ID[en]]
            if forced:
                canon[en] = forced[0]
                continue
        kana = [r for r in nf if _is_kana(r)]
        canon[en] = min(kana or nf, key=len)
    return canon


# Compass / movement words -> reading (validated against ZVOCTBL in __main__).
DIRECTIONS = {
    "NORTH": "きた", "SOUTH": "みなみ", "EAST": "ひがし", "WEST": "にし",
    "UP": "うえ", "DOWN": "した",
    "NORTHEAST": "ほくとう", "NORTHWEST": "ほくせい", "SOUTHEAST": "なんとう", "SOUTHWEST": "なんせい",
    "ENTER": "はいる", "EXIT": "でる",
}

# English synonyms the player may type/select -> a canonical English word that exists in CANON or
# DIRECTIONS. Keep targets verified (see __main__). Abbreviations included for typed play.
SYNONYMS = {
    # movement
    "N": "NORTH", "S": "SOUTH", "E": "EAST", "W": "WEST", "U": "UP", "D": "DOWN",
    "NE": "NORTHEAST", "NW": "NORTHWEST", "SE": "SOUTHEAST", "SW": "SOUTHWEST",
    "GO": "WALK", "RUN": "WALK", "PROCEED": "WALK",
    # take / drop
    "GET": "TAKE", "GRAB": "TAKE", "HOLD": "TAKE", "CARRY": "TAKE", "CATCH": "TAKE",
    "RELEASE": "DROP",
    # look / examine
    "L": "LOOK", "X": "EXAMINE", "INSPECT": "EXAMINE", "DESCRIBE": "EXAMINE", "WATCH": "LOOK",
    # combat
    "KILL": "ATTACK", "FIGHT": "ATTACK", "STRIKE": "HIT", "SLAY": "ATTACK", "MURDER": "ATTACK",
    # containers / misc common
    "SHUT": "CLOSE", "UNLATCH": "UNLOCK", "LIGHT": "TURN ON",
    # (INVENTORY/I has no ZVOCTBL reading -- it's a UI/meta command, not parser vocab.)
}


def normalize(word):
    return word.strip().upper()


def resolve(word, canon=None):
    """English word -> reading, or None. Applies the synonym layer then CANON/DIRECTIONS."""
    if canon is None:
        canon = build_canon()
    w = normalize(word)
    seen = set()
    while w in SYNONYMS and w not in seen:       # follow synonym chain (guard against cycles)
        seen.add(w)
        w = SYNONYMS[w]
    if w in canon:
        return canon[w]
    if w in DIRECTIONS:
        return DIRECTIONS[w]
    return None


def translate_command(text, canon=None):
    """English command line -> (jp_reading_tokens, unknown_words). Articles are dropped."""
    if canon is None:
        canon = build_canon()
    drop = {"THE", "A", "AN", "AT", "TO", "OF"}     # ignored fillers (real preps need SYNTBL)
    out, unknown = [], []
    for tok in text.replace(",", " ").split():
        w = normalize(tok)
        if w in drop:
            continue
        r = resolve(w, canon)
        (out if r else unknown).append(r if r else w)
    # movement: "go north" -> just the direction (directions parse standalone), so drop a
    # redundant movement verb when a direction reading is present.
    dirs = set(DIRECTIONS.values())
    if any(t in dirs for t in out):
        walk = resolve("WALK", canon)
        out = [t for t in out if t != walk]
    return out, unknown


# --- full command translation: English -> Japanese command string (SOV + particles) -------------
def _has_kanji(s):
    return any(0x3400 <= ord(c) <= 0x9FFF for c in s)


# Zork I English adjectives (object disambiguators). POS from ZVOCTBL flags is unreliable
# (fixed-stride misparse flags some verbs as adj), so adjective-ness is pinned explicitly here.
ADJECTIVES = {
    "SMALL", "LARGE", "WHITE", "BLACK", "RED", "BLUE", "GREEN", "YELLOW", "BROWN", "OLD", "ANCIENT",
    "NARROW", "RUSTY", "SMELLY", "DEAD", "WOODEN", "GRANITE", "JADE", "ELVISH", "BLOODY", "BOARDED",
    "GLASS", "TROPHY", "TRAP", "CRYSTAL", "BRASS", "CLOCKWORK", "JEWELED", "BROKEN", "MAGIC",
    "CONTROL", "LEATHER", "GOLD", "SILVER", "PLATINUM",
}


def build_jp_lexicon():
    """English word -> (japanese DISPLAY form, pos in {verb,noun,adj,dir}).

    Forms prefer the kanji display form the menu uses (e.g. 郵便箱 over ゆうびんばこ, 小さな over
    ちいさな). POS priority: VERBS-value -> verb; ADJECTIVES -> adj; DIRECTIONS -> dir; else noun
    (ZVOCTBL flags are NOT trusted for POS — they misparse).
    """
    present = set(_present_readings())            # every reading ZVOCTBL accepts (any POS)
    en_form = {}
    def score(jp):                                # prefer: in-dictionary, then kanji, then shorter
        return (jp in present, _has_kanji(jp), -len(jp))
    def consider(en, jp):
        if en not in en_form or score(jp) > score(en_form[en]):
            en_form[en] = jp
    verb_en = set(VERBS.values())
    for jp, en in VERBS.items():                 # verb kanji dict-forms
        consider(en, jp)
    for jp, en in READING_EN.items():            # noun/adj forms
        consider(en, jp)

    def pos_of(en):
        if en in verb_en:    return "verb"
        if en in ADJECTIVES: return "adj"
        return "noun"
    lex = {en: (jp, pos_of(en)) for en, jp in en_form.items()}
    for en, jp in DIRECTIONS.items():
        lex[en] = (jp, "dir")
    return lex


# English preposition -> Japanese case particle (best-effort; Zork uses few).
PREP_PARTICLE = {"IN": "に", "INTO": "に", "INSIDE": "に", "ON": "に", "ONTO": "に", "AT": "に",
                 "WITH": "で", "USING": "で", "FROM": "から", "UNDER": "の下"}


def translate_command_to_japanese(text, lex=None):
    """English command -> (japanese command string, unknown words).

    Reorders to Japanese SOV and inserts particles: 'open small mailbox' -> '小さな郵便箱を開ける',
    'put lamp in case' -> 'ランプをケースに置く'. Articles dropped; spaceless output (as the parser
    sees it).
    """
    if lex is None:
        lex = build_jp_lexicon()
    drop = {"THE", "A", "AN", "OF"}
    verb_jp = None; direction = None
    groups = []                                   # (particle, [adj forms], noun form) in input order
    pending_adjs = []; pending_particle = "を"
    unknown = []
    for raw in text.replace(",", " ").split():
        w = normalize(raw)
        if w in drop:
            continue
        w = SYNONYMS.get(w, w)
        ent = lex.get(w)
        if ent is None:
            if w in PREP_PARTICLE:
                pending_particle = PREP_PARTICLE[w]
            else:
                unknown.append(w)
            continue
        jp, pos = ent
        if pos == "verb":
            verb_jp = jp
        elif pos == "adj":
            pending_adjs.append(jp)
        elif pos == "dir":
            direction = jp
        else:                                     # noun -> close current object group
            groups.append((pending_particle, pending_adjs, jp))
            pending_adjs = []; pending_particle = "を"
    out = ""
    for particle, adjs, noun in groups:
        out += "".join(adjs) + noun + particle
    if direction:                                 # movement: the direction alone parses
        out += direction
    elif verb_jp:
        out += verb_jp
    return out, unknown


if __name__ == "__main__":
    sys.stdout = __import__("io").TextIOWrapper(sys.stdout.buffer, encoding="utf-8")
    canon = build_canon()
    rid = _present_readings()
    print("CANON entries:", len(canon))

    # validate DIRECTIONS + SYNONYM targets
    bad_dir = {e: r for e, r in DIRECTIONS.items() if r not in rid}
    print("DIRECTIONS missing reading:", bad_dir or "none")
    bad_syn = {s: t for s, t in SYNONYMS.items() if t not in canon and t not in DIRECTIONS}
    print("SYNONYM targets not resolvable:", bad_syn or "none")

    # demo reading-token translations
    for cmd in ["take lamp", "open the mailbox", "read leaflet", "go north", "n"]:
        toks, unk = translate_command(cmd, canon)
        print("  %-26s -> %s   unknown=%s" % (cmd, " ".join(toks), unk or "-"))

    # full command -> Japanese sentence, verified against the known menu command
    print()
    lex = build_jp_lexicon()
    present = set(_present_readings())
    EXPECT = {"open small mailbox": "小さな郵便箱を開ける"}   # confirmed on-screen menu command
    cmds = ["open small mailbox", "open the mailbox", "examine mailbox", "take lamp",
            "read leaflet", "put lamp in case", "kill troll with sword", "go north",
            "open trap door", "take jeweled egg"]
    for cmd in cmds:
        jp, unk = translate_command_to_japanese(cmd, lex)
        tag = ""
        if cmd in EXPECT:
            tag = "  [MATCH]" if jp == EXPECT[cmd] else "  [*** want %s]" % EXPECT[cmd]
        u = "  unknown=%s" % unk if unk else ""
        print("  %-26s -> %s%s%s" % (cmd, jp, u, tag))

    # parse-ability coverage: which chosen word-forms are NOT in ZVOCTBL (would fail to parse)
    print()
    notpresent = sorted({en: jp for en, (jp, _p) in lex.items() if jp not in present}.items())
    print("lexicon: %d words, %d with a ZVOCTBL-present form, %d WITHOUT (would not parse):"
          % (len(lex), len(lex) - len(notpresent), len(notpresent)))
    print("  no-parse-form:", ", ".join("%s=%s" % (e, j) for e, j in notpresent[:30]))
