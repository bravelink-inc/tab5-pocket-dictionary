#!/usr/bin/env python3
"""screenshot.py - grab the Tab5 screen over the USB-C serial port.

Usage: screenshot.py [-p PORT] [-o out.png] [--type TEXT]
  --type TEXT   inject TEXT as key presses before the screenshot (letters, digits, space,
                \\b backspace, \\n enter, \\t tab; escape sequences like \\x1b[B for arrows)
Requires: pyserial. Writes a PNG (no Pillow needed).
"""
import argparse, base64, struct, sys, time, zlib
import serial
from tab5port import find_tab5_port, utf8_stdout

# [book:15-png]
def png(w, h, rgb565_rows):
    raw = bytearray()
    for row in rgb565_rows:
        raw.append(0)
        for i in range(0, len(row), 2):
            v = (row[i] << 8) | row[i + 1]   # M5GFX readRect returns big-endian RGB565
            r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
            raw += bytes(((r * 255) // 31, (g * 255) // 63, (b * 255) // 31))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))
# [/book:15-png]

def capture(s):
    """Ask the firmware for one frame dump; return ({row: bytes}, width, height)."""
    s.reset_input_buffer()
    s.write(b"S"); s.flush()
    w = h = None; rows = {}; t0 = time.time()
    while time.time() - t0 < 120:
        line = s.readline().decode("ascii", "replace").strip()
        if not line: continue
        if line.startswith("PDSHOT END"): break
        if line.startswith("PDSHOT "):
            _, w, h = line.split(); w, h = int(w), int(h); continue
        if w is None or not line.startswith("R") or ":" not in line: continue
        idx, _, payload = line.partition(":")
        try:
            y = int(idx[1:]); row = base64.b64decode(payload, validate=True)
        except Exception:
            continue
        if len(row) == w * 2 and 0 <= y < h: rows[y] = row
    return rows, w, h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", default=None, help="serial port (auto-detected when omitted; e.g. COM3 or /dev/cu.usbmodem1101)")
    ap.add_argument("-o", "--output", default="screenshot.png")
    ap.add_argument("--type", default=None)
    ap.add_argument("--wait", type=float, default=0.8)
    a = ap.parse_args()
    utf8_stdout()
    s = serial.Serial(find_tab5_port(a.port), 115200, timeout=2)
    s.reset_input_buffer()
    if a.type is not None:
        text = a.type.encode().decode("unicode_escape")
        for ch in text:
            s.write(ch.encode("latin-1")); s.flush(); time.sleep(0.06)
        time.sleep(a.wait)
    rows, w, h = {}, None, None
    for attempt in range(1, 4):          # the screen is static, so merge rows across attempts
        got, w, h = capture(s)
        rows.update(got)
        if h and len(rows) == h:
            break
        print(f"attempt {attempt}: {len(rows)} of {h} rows so far, capturing again", file=sys.stderr)
    if not rows:
        print("no screenshot data received", file=sys.stderr); sys.exit(1)
    missing = [y for y in range(h) if y not in rows]
    if missing:
        print(f"warning: {len(missing)} row(s) still missing; copied from the nearest row", file=sys.stderr)
        have = sorted(rows)
        for y in missing:
            rows[y] = rows[min(have, key=lambda r: abs(r - y))]
    open(a.output, "wb").write(png(w, h, [rows[y] for y in range(h)]))
    print(f"{a.output}: {w}x{h}")

if __name__ == "__main__":
    main()
