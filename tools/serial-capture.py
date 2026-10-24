#!/usr/bin/env python3
"""Capture the serial output of an example for a fixed duration.

Usage: python3 tools/serial-capture.py <seconds> [baud]

Prints the captured output to stdout. Exits 1 if the port cannot be opened.
The DTR/RTS toggle restarts the ESP32, so the capture starts from setup().
"""
import sys
import time

import serial


def main() -> int:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    port = "/dev/cu.usbserial-0001"

    try:
        ser = serial.Serial(port, baud, timeout=0.5)
    except serial.SerialException as e:
        print(f"ERROR: cannot open {port}: {e}", file=sys.stderr)
        return 1

    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    ser.reset_input_buffer()

    deadline = time.time() + seconds
    buf = b""
    while time.time() < deadline:
        chunk = ser.read(4096)
        if chunk:
            buf += chunk

    ser.close()
    sys.stdout.write(buf.decode("utf-8", errors="replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())