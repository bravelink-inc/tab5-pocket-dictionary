"""tab5port.py - find the serial port of a connected M5Stack Tab5 (ESP32-P4 USB Serial/JTAG).

Works on macOS (/dev/cu.usbmodem*), Windows (COMx) and Linux (/dev/ttyACM*).
"""
import sys


def utf8_stdout():
    """Make print() safe for Japanese on Windows consoles (cp932) and pipes."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


def list_candidates():
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    espressif = [p for p in ports if (p.vid == 0x303A) or ("JTAG" in (p.description or "").upper())]
    return espressif, ports


def find_tab5_port(explicit=None):
    """Return the port name to use. `explicit` (from -p) wins; otherwise auto-detect."""
    if explicit:
        return explicit
    espressif, ports = list_candidates()
    if len(espressif) == 1:
        return espressif[0].device
    names = ", ".join(f"{p.device} ({p.description})" for p in ports) or "none"
    if not espressif:
        sys.exit(f"Tab5 not found. Connect the USB-C port and check the cable. Serial ports seen: {names}\n"
                 f"(specify one with -p, e.g. -p COM3 or -p /dev/cu.usbmodem1101)")
    sys.exit(f"Several Espressif devices found; specify one with -p: "
             + ", ".join(p.device for p in espressif))
