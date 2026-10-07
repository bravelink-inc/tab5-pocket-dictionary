#!/usr/bin/env python3
"""sdput.py - copy files to the Tab5's SD card over the USB-C serial port (no card reader needed).

  sdput.py file1.pdc file2.pdc ...        # -> /sdcard/dict/<name>, then reload dictionaries
  sdput.py --ls [/sdcard/dict]            # list a directory
  sdput.py --rm /sdcard/dict/old.pdc      # delete a file
  sdput.py --free                         # heap statistics
Options: -p PORT (auto-detected when omitted; e.g. COM3 on Windows), -d DEST_DIR (default /sdcard/dict), --no-reload
"""
import argparse, base64, os, sys, time
import serial
from tab5port import find_tab5_port, utf8_stdout

CHUNK = 6144   # raw bytes per line (8 KB of base64)


def readline(s, timeout=20.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        line = s.readline()
        if line:
            return line.decode('utf-8', 'replace').strip()
    return None


def expect(s, prefix, timeout=20.0):
    while True:
        line = readline(s, timeout)
        if line is None:
            raise RuntimeError(f'timeout waiting for {prefix}')
        if line.startswith(prefix):
            return line
        if line.startswith('ERR'):
            raise RuntimeError(line)
        # ignore log noise


def command(s, text):
    s.reset_input_buffer()
    s.write(b'\x01' + text.encode() + b'\n'); s.flush()


# [book:15-sdput-put]
def put(s, path, dest):
    size = os.path.getsize(path)
    command(s, f'PUT {dest} {size}')
    expect(s, 'READY')
    sent = 0
    t0 = time.time()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(CHUNK)
            if not chunk:
                break
            s.write(base64.b64encode(chunk) + b'\n'); s.flush()
            expect(s, 'OK')
            sent += len(chunk)
            if sent % (CHUNK * 64) == 0 or sent == size:
                el = time.time() - t0
                print(f'\r  {os.path.basename(path)}: {sent*100//size}% ({sent/1024/1024:.1f} MB, {sent/1024/max(el,0.01):.0f} KB/s)', end='', flush=True)
    done = expect(s, 'DONE')
    print(f'\r  {os.path.basename(path)} -> {dest}: {done}' + ' ' * 20)
# [/book:15-sdput-put]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*')
    ap.add_argument('-p', '--port', default=None, help='serial port (auto-detected when omitted; e.g. COM3 or /dev/cu.usbmodem1101)')
    ap.add_argument('-d', '--dest', default='/sdcard/dict')
    ap.add_argument('--ls', nargs='?', const='/sdcard/dict')
    ap.add_argument('--rm')
    ap.add_argument('--free', action='store_true')
    ap.add_argument('--no-reload', action='store_true')
    a = ap.parse_args()
    utf8_stdout()
    s = serial.Serial(find_tab5_port(a.port), 115200, timeout=0.5)
    if a.ls is not None:
        command(s, f'LS {a.ls}')
        while True:
            line = readline(s)
            if line is None or line == 'END': break
            if line.startswith('ERR'): print(line); break
            if line and (line[0] in 'd-') and line[1] == ' ': print(line)
        return
    if a.rm:
        command(s, f'RM {a.rm}'); print(readline(s)); return
    if a.free:
        command(s, 'FREE'); print(expect(s, 'internal')); return
    if not a.files:
        ap.print_help(); return
    for f in a.files:
        put(s, f, f'{a.dest}/{os.path.basename(f)}')
    if not a.no_reload:
        command(s, 'RELOAD'); expect(s, 'OK'); print('dictionaries reloaded')


if __name__ == '__main__':
    try:
        main()
    except RuntimeError as e:
        print(f'error: {e}', file=sys.stderr); sys.exit(1)
