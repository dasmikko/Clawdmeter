#!/usr/bin/env python3
"""Clawdmeter now-playing watcher (Linux, MPRIS via playerctl).

Polls `playerctl -p playerctld metadata` once a second and writes the current
track as a device payload to OUT_FILE (atomically), only when something the
device cares about changed. The BLE daemon forwards the file's content to the
device whenever it changes, so every GATT write stays in the daemon's loop.

Why poll instead of `playerctl --follow`: following through playerctld (the
proxy that tracks the most recently active player) prints no metadata at all
in playerctl 2.4, and following without it pins one arbitrary player. A
one-shot query every second is cheap and also catches seeks, which emit no
metadata change.

Payload: {"np":{"st":"Playing","ti":"...","ar":"...","al":"...","len":225,"pos":12}}
  st   Playing | Paused | Stopped (Stopped also when no player is running)
  len  track length in s (0 = unknown, e.g. live streams); pos = position in s

The device fonts are ASCII-only, so text is folded to ASCII (ø -> o, é -> e).

    media_watch.py OUT_FILE
"""

import json
import os
import subprocess
import sys
import time
import unicodedata

FIELDS = ("status", "xesam:title", "xesam:artist", "xesam:album", "mpris:length", "position")
FORMAT = "\x1f".join("{{%s}}" % f for f in FIELDS)
SEEK_TOLERANCE_S = 3     # resend when the position drifts this far from extrapolation
TEXT_LIMITS = {"ti": 120, "ar": 80, "al": 80}   # match the firmware's MediaInfo buffers
MAX_PAYLOAD = 500        # firmware RX buffer is 512 bytes incl. NUL

# Letters NFKD can't decompose into ASCII.
FOLD = str.maketrans({
    "æ": "ae", "Æ": "AE", "ø": "o", "Ø": "O", "œ": "oe", "Œ": "OE", "ß": "ss",
    "đ": "d", "Đ": "D", "ł": "l", "Ł": "L", "þ": "th", "Þ": "Th", "ð": "d",
    "‘": "'", "’": "'", "‚": "'", "“": '"', "”": '"', "„": '"',
    "–": "-", "—": "-", "…": "...", "•": "-", "·": "-", "×": "x",
})


def to_ascii(text: str, limit: int) -> str:
    text = unicodedata.normalize("NFKD", text.translate(FOLD))
    text = "".join(c for c in text if c.isascii() and (c.isprintable() or c == " "))
    text = " ".join(text.split())
    return text[:limit].rstrip()


def query():
    """Current track as a dict, or None when no player is running."""
    try:
        out = subprocess.run(
            ["playerctl", "-p", "playerctld", "metadata", "--format", FORMAT],
            capture_output=True, text=True, timeout=3,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if out.returncode != 0:
        return None
    parts = out.stdout.rstrip("\n").split("\x1f")
    if len(parts) != len(FIELDS):
        return None
    status, title, artist, album, length_us, pos_us = parts

    def secs(us):
        try:
            return max(int(float(us)) // 1_000_000, 0)
        except ValueError:
            return 0

    return {
        "st": status if status in ("Playing", "Paused", "Stopped") else "Stopped",
        "ti": to_ascii(title, TEXT_LIMITS["ti"]),
        "ar": to_ascii(artist, TEXT_LIMITS["ar"]),
        "al": to_ascii(album, TEXT_LIMITS["al"]),
        "len": secs(length_us),
        "pos": secs(pos_us),
    }


def encode(payload: dict) -> str:
    """JSON for the device, trimming the longest text field until it fits.
    Escapes (quotes, backslashes) can push a max-length payload past the
    firmware's RX buffer, which would truncate it into invalid JSON."""
    p = dict(payload)
    while True:
        text = json.dumps({"np": p}, separators=(",", ":"))
        if len(text.encode()) <= MAX_PAYLOAD:
            return text
        key = max(("ti", "ar", "al"), key=lambda k: len(p[k]))
        p[key] = p[key][:max(len(p[key]) - 8, 0)]


def write_atomic(path: str, payload: dict) -> None:
    tmp = f"{path}.{os.getpid()}.tmp"
    with open(tmp, "w") as f:
        f.write(encode(payload) + "\n")
    os.replace(tmp, path)


def main(out_path: str) -> None:
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    sent = None          # last payload written
    sent_at = 0.0        # monotonic time of that write
    while True:
        cur = query() or {"st": "Stopped", "ti": "", "ar": "", "al": "", "len": 0, "pos": 0}
        changed = sent is None or any(cur[k] != sent[k] for k in ("st", "ti", "ar", "al", "len"))
        if not changed and cur["st"] == "Playing":
            expected = sent["pos"] + (time.monotonic() - sent_at)
            changed = abs(cur["pos"] - expected) > SEEK_TOLERANCE_S
        elif not changed:
            changed = abs(cur["pos"] - sent["pos"]) > SEEK_TOLERANCE_S   # seek while paused
        if changed:
            write_atomic(out_path, cur)
            sent, sent_at = cur, time.monotonic()
        time.sleep(1)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    try:
        main(sys.argv[1])
    except KeyboardInterrupt:
        pass
