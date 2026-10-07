#!/usr/bin/env python3
"""
build_dict.py - Convert TSV dictionary files into the PDC1 binary format used by
the M5Stack Tab5 pocket dictionary firmware.

Input : one or more UTF-8 text files, one entry per line: ``headword<TAB>definition``.
        Several spellings may share one line: ``color, colour<TAB>definition``.
Output: a single .pdc file. Copy it to ``/dict/`` on the SD card, or let the firmware
        build flash it to the ``dict`` partition (data/ejdict.pdc).

Layout (little-endian, all offsets relative to file start):

    header  (64 bytes)
        char     magic[4]      "PDC1"
        uint32   version       1
        uint32   entry_count   number of keys
        uint32   flags         0
        uint32   keys_off      normalized keys, each NUL-terminated, sorted bytewise
        uint32   keys_size
        uint32   kidx_off      uint32[entry_count]  offset of key i inside the keys blob
        uint32   dref_off      uint32[entry_count]  offset of the record for key i inside defs
        uint32   defs_off      records: uint32 len | headword '\0' definition '\0'
        uint32   defs_size
        uint32   title_off     NUL-terminated UTF-8 dictionary title
        uint32   total_size
        char     tag[16]       short dictionary tag shown in cross-dictionary results (NUL-terminated)

Keys are normalized with NFKC + lowercase + whitespace collapsing so the firmware can do a
plain bytewise binary search on what the user types.
"""
import argparse
import os
import re
import struct
import sys
import unicodedata

MAGIC = b"PDC1"
VERSION = 1
HEADER_FMT = "<4sIIIIIIIIIII16s"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
assert HEADER_SIZE == 64


# [book:7-normalize-key]
def normalize_key(word: str) -> str:
    w = unicodedata.normalize("NFKC", word).strip().lower()
    w = re.sub(r"\s+", " ", w)
    return w
# [/book:7-normalize-key]


def align4(buf: bytearray) -> None:
    while len(buf) % 4:
        buf.append(0)


# [book:7-parse-line]
def parse_line(line: str, headword_sep: str = ""):
    line = line.rstrip("\r\n")
    if not line or line.startswith("#") or "\t" not in line:
        return None
    head, definition = line.split("\t", 1)
    head = head.strip()
    definition = definition.strip().replace("\t", " ")
    if not head or not definition:
        return None
    # "a, b, c" -> several spellings of the same entry. A comma without a following
    # space (e.g. "1,000") is kept as part of the headword.
    spellings = [h.strip() for h in head.split(", ")] if ", " in head else [head]
    spellings = [s for s in spellings if s]
    # With --headword-sep the first field only holds search keys and the definition
    # starts with "display headword<sep>".
    if headword_sep and headword_sep in definition:
        head, definition = definition.split(headword_sep, 1)
        head = head.strip()
        if not head or not definition:
            return None
    return head, spellings, definition
# [/book:7-parse-line]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="TSV files (headword<TAB>definition)")
    ap.add_argument("-o", "--output", required=True, help="output .pdc file")
    ap.add_argument("--title", required=True, help="dictionary title shown on screen (UTF-8)")
    ap.add_argument("--encoding", default="utf-8", help="input encoding (default utf-8)")
    ap.add_argument("--tag", default="", help="short tag (<=15 bytes UTF-8) shown next to results in cross-dictionary search")
    ap.add_argument("--headword-sep", default="", help="if set, the first column is only search keys and the definition is 'headword<sep>definition'")
    args = ap.parse_args()
    tag = args.tag.encode("utf-8")[:15]

    records = []          # (headword, definition)
    record_ids = {}       # (headword, definition) -> record index
    keys = []             # (normalized key bytes, record index)
    seen = set()          # (key, record) dedupe
    skipped = 0

    for path in args.inputs:
        with open(path, encoding=args.encoding, errors="replace") as fh:
            for line in fh:
                parsed = parse_line(line, args.headword_sep)
                if parsed is None:
                    if line.strip():
                        skipped += 1
                    continue
                head, spellings, definition = parsed
                rec_key = (head, definition)
                rid = record_ids.get(rec_key)
                if rid is None:
                    rid = len(records)
                    records.append(rec_key)
                    record_ids[rec_key] = rid
                for sp in spellings:
                    nk = normalize_key(sp).encode("utf-8")
                    if not nk or (nk, rid) in seen:
                        continue
                    seen.add((nk, rid))
                    keys.append((nk, rid))

    if not keys:
        print("no entries found", file=sys.stderr)
        return 1

    keys.sort(key=lambda kr: (kr[0], kr[1]))

# [book:7-write-layout]
    # --- defs blob -----------------------------------------------------------
    defs = bytearray()
    rec_off = []
    for head, definition in records:
        body = head.encode("utf-8") + b"\0" + definition.encode("utf-8") + b"\0"
        rec_off.append(len(defs))
        defs += struct.pack("<I", len(body)) + body
        align4(defs)

    # --- keys blob + indexes -------------------------------------------------
    kblob = bytearray()
    kidx = []
    dref = []
    for nk, rid in keys:
        kidx.append(len(kblob))
        kblob += nk + b"\0"
        dref.append(rec_off[rid])
    align4(kblob)

    n = len(keys)
    title = args.title.encode("utf-8") + b"\0"

    keys_off = HEADER_SIZE
    kidx_off = keys_off + len(kblob)
    dref_off = kidx_off + 4 * n
    defs_off = dref_off + 4 * n
    title_off = defs_off + len(defs)
    total = title_off + len(title)
    total += (-total) % 4

    header = struct.pack(HEADER_FMT, MAGIC, VERSION, n, 0,
                         keys_off, len(kblob), kidx_off, dref_off,
                         defs_off, len(defs), title_off, total, tag.ljust(16, b"\0"))

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)   # e.g. data/ in a fresh clone
    with open(args.output, "wb") as out:
        out.write(header)
        out.write(kblob)
        out.write(struct.pack("<%dI" % n, *kidx))
        out.write(struct.pack("<%dI" % n, *dref))
        out.write(defs)
        out.write(title)
        out.write(b"\0" * ((-total_written(out)) % 4))

# [/book:7-write-layout]
    print(f"{args.output}: {n} keys, {len(records)} records, {total} bytes"
          + (f", {skipped} malformed lines skipped" if skipped else ""))
    return 0


def total_written(fh) -> int:
    return fh.tell()


if __name__ == "__main__":
    sys.exit(main())
