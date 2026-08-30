"""Command-line interface for the Saturn translation pipeline.

Runs the same workflow as the MCP tools without needing an MCP client. Useful
for scripting, batch jobs, and triaging candidate games. Does not import
fastmcp, so it works with only the core dependencies installed.

    saturn-translate-cli feasibility GAME.iso
    saturn-translate-cli list GAME.iso
    saturn-translate-cli extract GAME.iso /DATA/MSG.BIN -o work/MSG.BIN
    saturn-translate-cli scan work/MSG.BIN
    saturn-translate-cli tables work/MSG.BIN
    saturn-translate-cli project work/MSG.BIN -o work/MSG.json
    saturn-translate-cli translate work/MSG.json
    saturn-translate-cli apply work/MSG.json --strategy in_place -o work/MSG.en
    saturn-translate-cli pack GAME.iso /DATA/MSG.BIN work/MSG.en GAME_EN.iso
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import sjis, pointers, reinsert, build, triage, vcdiff, ips, langpatch, ecc, sh2
from .iso import SaturnImage
from .project import TranslationProject, StringEntry


def _read(path: str) -> bytes:
    with open(path, "rb") as fh:
        return fh.read()


# ── commands ───────────────────────────────────────────────────────────
def cmd_feasibility(args) -> int:
    scores = triage.analyze_image(_read(args.image), min_string_count=args.min_strings)
    if args.json:
        print(json.dumps([s.to_dict() for s in scores], ensure_ascii=False, indent=2))
        return 0
    if not scores:
        print("No files with Japanese text found.")
        return 0
    print(f"\nTranslation feasibility for {args.image}")
    print(f"{'difficulty':<10} {'score':>5}  {'strings':>7} {'jp_chars':>8} "
          f"{'table':>6} {'fits':>5}  file")
    print("-" * 78)
    for s in scores[: args.top or len(scores)]:
        table = f"{s.best_table_coverage*100:.0f}%" if s.has_pointer_table else "-"
        print(f"{s.difficulty:<10} {s.score:>5.0f}  {s.string_count:>7} "
              f"{s.jp_char_total:>8} {table:>6} {s.est_fit_ratio*100:>4.0f}%  {s.path}")
    easiest = scores[0]
    print(f"\nStart with: {easiest.path}  ({easiest.difficulty}, "
          f"{easiest.string_count} strings, "
          f"{easiest.est_fit_ratio*100:.0f}% likely fit in place)")
    return 0


def cmd_list(args) -> int:
    img = SaturnImage.from_file(args.image)
    files = img.list_files()
    print(f"{img.sector_size}-byte sectors, {len(files)} entries")
    for f in files:
        kind = "DIR " if f.is_dir else "    "
        print(f"  {kind} {f.size:>10}  {f.path}")
    return 0


def cmd_extract(args) -> int:
    img = SaturnImage.from_file(args.image)
    data = img.extract(args.file)
    out = args.out or Path(args.file).name
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    with open(out, "wb") as fh:
        fh.write(data)
    print(f"extracted {len(data)} bytes -> {out}")
    return 0


def cmd_scan(args) -> int:
    hits = sjis.scan(_read(args.file), min_chars=args.min_chars)
    print(f"{len(hits)} string(s) in {args.file}")
    for h in hits[: args.limit]:
        print(f"  @{h.offset:#08x} ({h.length:>3}b) {h.text!r}")
    if len(hits) > args.limit:
        print(f"  ... {len(hits) - args.limit} more")
    return 0


def cmd_tables(args) -> int:
    data = _read(args.file)
    hits = sjis.scan(data)
    tables = pointers.detect_tables(data, [h.offset for h in hits], min_entries=args.min_entries)
    print(f"{len(tables)} pointer table(s) in {args.file}")
    for t in tables[:20]:
        print(f"  @{t.table_offset:#08x}  {t.count} entries  base={t.base:#010x}  "
              f"width={t.width}  {'BE' if t.big_endian else 'LE'}")
    return 0


def cmd_project(args) -> int:
    data = _read(args.file)
    scan_hits = sjis.scan(data, min_chars=args.min_chars)
    tables = pointers.detect_tables(data, [h.offset for h in scan_hits], min_entries=3)
    base = tables[0].base if tables else args.base
    if tables:
        offsets = sorted({off for t in tables for off in t.entries})
        hits = [sjis.decode_string_at(data, o) for o in offsets]
    else:
        hits = scan_hits

    proj = TranslationProject(source_file=args.file, image_path=args.image or "", base_address=base)
    for i, h in enumerate(hits):
        pts = [p.pointer_offset for p in pointers.find_pointers_to(data, h.offset, bases=(base,))]
        proj.strings.append(StringEntry(id=i, offset=h.offset, length=h.length,
                                        original=h.text, pointer_offsets=pts))
    out = args.out or (Path(args.file).stem + ".translation.json")
    proj.save(out)
    print(f"project -> {out}  (base={base:#010x}, {len(proj.strings)} strings, "
          f"{sum(1 for s in proj.strings if s.pointer_offsets)} with pointers)")
    return 0


def cmd_translate(args) -> int:
    from .textra import translate_many, TextraError
    proj = TranslationProject.load(args.project)
    todo = [s for s in proj.strings if not s.translation.strip()]
    if args.limit:
        todo = todo[: args.limit]
    if not todo:
        print("nothing to translate")
        return 0
    try:
        res = translate_many([s.original for s in todo])
    except TextraError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    for s, en in zip(todo, res):
        s.translation = en
    proj.save(args.project)
    print(f"translated {len(todo)} string(s); {proj.stats()['percent_complete']}% complete")
    return 0


def cmd_apply(args) -> int:
    proj = TranslationProject.load(args.project)
    data = bytearray(_read(proj.source_file))
    edits = [reinsert.Edit(s.offset, s.length, s.translation, s.pointer_offsets)
             for s in proj.strings if s.translation.strip()]
    if args.strategy == "in_place":
        r = reinsert.reinsert_in_place(data, edits)
        print(f"in_place: wrote {r.written}, {len(r.overflowed)} overflowed/truncated")
    else:
        r = reinsert.reinsert_repoint(data, edits, base=proj.base_address,
                                      width=proj.pointer_width, big_endian=proj.big_endian)
        print(f"repoint: wrote {r.written}, appended {r.appended_bytes}b, "
              f"repointed {r.repointed} slot(s)")
    out = args.out or (proj.source_file + ".en")
    with open(out, "wb") as fh:
        fh.write(data)
    print(f"-> {out} ({len(data)} bytes)")
    return 0


def cmd_pack(args) -> int:
    out_bytes, res = build.replace_file(_read(args.image), args.file, _read(args.patched))
    Path(args.out_image).parent.mkdir(parents=True, exist_ok=True)
    with open(args.out_image, "wb") as fh:
        fh.write(out_bytes)
    print(f"patched {args.file}: {res.original_size} -> {res.new_size} bytes "
          f"({res.allocated_bytes} allocated) -> {args.out_image}")
    return 0


def cmd_disasm(args) -> int:
    """Disassemble SH-2 code from a raw file (e.g. an extracted 0.BIN)."""
    data = _read(args.file)
    base = args.base
    insns = sh2.disasm_range(data, args.offset, args.count, base)
    print(sh2.format_listing(insns))
    return 0


def cmd_force_language(args) -> int:
    """Detect the boot-language selector and emit xdelta + ips patches that force
    the English overlay chain. Operates on the disc image's Track 1 .bin."""
    src = _read(args.image)
    img = SaturnImage(src)
    flip = langpatch.find_language_flip(
        img, boot_file=args.boot_file,
        jp_name=args.jp_name.encode(), en_name=args.en_name.encode(),
    )
    if flip is None:
        print("Could not locate a language selector "
              f"({args.jp_name}/{args.en_name}) in {args.boot_file}.", file=sys.stderr)
        return 1
    print(flip.describe())

    # sanity: confirm current bytes match what we expect before patching
    if src[flip.image_offset:flip.image_offset + 4] != flip.old_bytes:
        print("warning: bytes at target offset don't match expected pointer; "
              "aborting to avoid a bad patch.", file=sys.stderr)
        return 1

    edits = [vcdiff.Edit(offset=flip.image_offset, old_len=4, data=flip.new_bytes)]
    ips_records = [ips.Record(offset=flip.image_offset, data=flip.new_bytes)]

    if args.fix_ecc:
        # Recompute EDC/ECC for the sector containing the flipped pointer so that
        # accurate CD-block emulation (e.g. Terraonion MODE) accepts it.
        sec_idx = flip.image_offset // ecc.SECTOR_SIZE
        sec_start = sec_idx * ecc.SECTOR_SIZE
        sector = bytearray(src[sec_start:sec_start + ecc.SECTOR_SIZE])
        # apply the pointer flip inside the sector copy, then fix its ECC
        local = flip.image_offset - sec_start
        sector[local:local + 4] = flip.new_bytes
        ecc.fix_sector(sector)
        ecc_block = bytes(sector[ecc.EDC_OFFSET:0x930])   # EDC + reserved + P + Q
        ecc_off = sec_start + ecc.EDC_OFFSET
        edits.append(vcdiff.Edit(offset=ecc_off, old_len=len(ecc_block), data=ecc_block))
        ips_records.append(ips.Record(offset=ecc_off, data=ecc_block))
        print(f"fix-ecc: recomputed EDC/ECC for sector {sec_idx} "
              f"(image {ecc_off:#x}, {len(ecc_block)} bytes)")

    patch = vcdiff.encode(src, edits)

    # Build the expected patched image (apply all edits) for self-verification.
    expected = bytearray(src)
    for e in edits:
        expected[e.offset:e.offset + e.old_len] = e.data
    rebuilt = vcdiff.decode(src, patch)
    ok = len(rebuilt) == len(expected)
    step = 8 << 20
    if ok:
        for s in range(0, len(expected), step):
            if rebuilt[s:s + step] != bytes(expected[s:s + step]):
                ok = False
                break
    if not ok:
        print("ERROR: xdelta self-verification failed.", file=sys.stderr)
        return 1

    stem = args.out_prefix or (Path(args.image).stem + "-english")
    xd = stem + ".xdelta"; ip = stem + ".ips"
    with open(xd, "wb") as fh:
        fh.write(patch)
    ips_patch = ips.encode(ips_records)
    with open(ip, "wb") as fh:
        fh.write(ips_patch)
    print(f"xdelta -> {xd} ({len(patch)} bytes, self-verified)")
    print(f"ips    -> {ip} ({len(ips_patch)} bytes)")
    if args.write_image:
        with open(args.write_image, "wb") as fh:
            fh.write(bytes(expected))
        print(f"patched image -> {args.write_image}")
    return 0


# ── parser ─────────────────────────────────────────────────────────────
def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="saturn-translate-cli",
                                description="Sega Saturn JA->EN translation pipeline.")
    sub = p.add_subparsers(dest="command", required=True)

    f = sub.add_parser("feasibility", help="rank disc files by translation difficulty")
    f.add_argument("image")
    f.add_argument("--top", type=int, default=0, help="show only the top N files")
    f.add_argument("--min-strings", type=int, default=1, dest="min_strings")
    f.add_argument("--json", action="store_true")
    f.set_defaults(func=cmd_feasibility)

    l = sub.add_parser("list", help="list files in a disc image")
    l.add_argument("image")
    l.set_defaults(func=cmd_list)

    e = sub.add_parser("extract", help="extract a file from a disc image")
    e.add_argument("image")
    e.add_argument("file")
    e.add_argument("-o", "--out")
    e.set_defaults(func=cmd_extract)

    s = sub.add_parser("scan", help="scan a file for Shift-JIS text")
    s.add_argument("file")
    s.add_argument("--min-chars", type=int, default=2, dest="min_chars")
    s.add_argument("--limit", type=int, default=50)
    s.set_defaults(func=cmd_scan)

    t = sub.add_parser("tables", help="detect pointer tables in a file")
    t.add_argument("file")
    t.add_argument("--min-entries", type=int, default=3, dest="min_entries")
    t.set_defaults(func=cmd_tables)

    pr = sub.add_parser("project", help="build a translation project from a file")
    pr.add_argument("file")
    pr.add_argument("-o", "--out")
    pr.add_argument("--image", help="record originating disc image path")
    pr.add_argument("--base", type=lambda x: int(x, 0), default=0,
                    help="pointer base if no table detected (e.g. 0x06000000)")
    pr.add_argument("--min-chars", type=int, default=2, dest="min_chars")
    pr.set_defaults(func=cmd_project)

    tr = sub.add_parser("translate", help="fill English via the Textra API")
    tr.add_argument("project")
    tr.add_argument("--limit", type=int, default=0)
    tr.set_defaults(func=cmd_translate)

    ap = sub.add_parser("apply", help="reinsert English into the source file")
    ap.add_argument("project")
    ap.add_argument("--strategy", choices=["in_place", "repoint"], default="in_place")
    ap.add_argument("-o", "--out")
    ap.set_defaults(func=cmd_apply)

    pk = sub.add_parser("pack", help="patch a translated file back into the image")
    pk.add_argument("image")
    pk.add_argument("file", help="in-image path to replace")
    pk.add_argument("patched", help="translated file on disk")
    pk.add_argument("out_image")
    pk.set_defaults(func=cmd_pack)

    fl = sub.add_parser("force-language",
                        help="emit xdelta+ips patches forcing the English boot overlays")
    fl.add_argument("image", help="disc image Track 1 .bin")
    fl.add_argument("--boot-file", default="/A.BIN", dest="boot_file")
    fl.add_argument("--jp-name", default="LOAD.BIN", dest="jp_name")
    fl.add_argument("--en-name", default="ELOAD.BIN", dest="en_name")
    fl.add_argument("-o", "--out-prefix", dest="out_prefix",
                    help="output patch path prefix (default: <image>-english)")
    fl.add_argument("--write-image", dest="write_image",
                    help="also write a full patched image to this path")
    fl.add_argument("--fix-ecc", action="store_true", dest="fix_ecc",
                    help="recompute EDC/ECC for the patched sector (needed for "
                         "strict ODEs like Terraonion MODE)")
    fl.set_defaults(func=cmd_force_language)

    da = sub.add_parser("disasm", help="disassemble SH-2 code from a raw file")
    da.add_argument("file", help="raw binary (e.g. extracted 0.BIN)")
    da.add_argument("--offset", type=lambda x: int(x, 0), default=0)
    da.add_argument("--count", type=int, default=64)
    da.add_argument("--base", type=lambda x: int(x, 0), default=0x06004000,
                    help="load address (Saturn IP default 0x06004000)")
    da.set_defaults(func=cmd_disasm)

    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
