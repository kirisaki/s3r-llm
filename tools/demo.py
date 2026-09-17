#!/usr/bin/env python3
"""Plays a short scripted conversation over the serial port, for demos.

Needs pyserial, which the ESP-IDF Python environment has:
    python tools/demo.py [/dev/ttyACM0]
"""
import sys
import time

import serial

LINES = [
    "Hello! How are you?",
    "What sound does a dog make?",
    "What sound does a cat make?",
    "My name is Tom. What is your name?",
    "Do you want to play with my red ball?",
    "What is the capital of France?",
    "Tell me about your day.",
    "Thanks, bye!",
]
PAUSE = 3.0  # seconds to read each reply


def wait_for_prompt(port, timeout):
    end = time.time() + timeout
    seen = b""
    while time.time() < end:
        data = port.read(256)
        sys.stdout.write(data.decode(errors="replace"))
        sys.stdout.flush()
        seen = (seen + data)[-8:]
        if seen.endswith(b"> "):
            return
    raise SystemExit("\nno prompt from the device")


def main():
    port = serial.Serial(sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0", 115200, timeout=0.1)
    # Reset the board so that the demo starts from an empty screen
    port.dtr = False
    port.rts = True
    time.sleep(0.1)
    port.rts = False
    wait_for_prompt(port, 20)
    time.sleep(PAUSE)
    for line in LINES:
        port.write(line.encode() + b"\r")
        wait_for_prompt(port, 120)
        time.sleep(PAUSE)
    print()


if __name__ == "__main__":
    main()
