"""Tests for daemon/media_watch.py (now-playing payloads from playerctl)."""

import json
import subprocess
from unittest import mock

from daemon import media_watch as mw


def fake_run(stdout, returncode=0):
    return mock.Mock(return_value=subprocess.CompletedProcess([], returncode, stdout=stdout, stderr=""))


def meta(*fields):
    return "\x1f".join(fields) + "\n"


def test_to_ascii_folds_nordic_and_accents():
    assert mw.to_ascii("Tøsedrengene – Æbler", 120) == "Tosedrengene - AEbler"
    assert mw.to_ascii("Sigur Rós", 120) == "Sigur Ros"
    assert mw.to_ascii("Beyoncé “Halo”", 120) == 'Beyonce "Halo"'


def test_to_ascii_drops_unmappable_and_collapses_space():
    assert mw.to_ascii("日本語  Mix\t", 120) == "Mix"


def test_to_ascii_truncates():
    assert mw.to_ascii("x" * 200, 120) == "x" * 120


def test_query_parses_playing_track():
    with mock.patch.object(mw.subprocess, "run", fake_run(
            meta("Playing", "Song", "Artist", "Album", "245000000", "83500000"))):
        assert mw.query() == {"st": "Playing", "ti": "Song", "ar": "Artist", "al": "Album",
                              "len": 245, "pos": 83}


def test_query_live_stream_has_zero_length():
    with mock.patch.object(mw.subprocess, "run", fake_run(meta("Playing", "Radio", "", "", "", "0"))):
        assert mw.query()["len"] == 0


def test_query_no_player_is_none():
    with mock.patch.object(mw.subprocess, "run", fake_run("", returncode=1)):
        assert mw.query() is None


def test_query_unknown_status_is_stopped():
    with mock.patch.object(mw.subprocess, "run", fake_run(meta("Weird", "a", "b", "c", "1", "0"))):
        assert mw.query()["st"] == "Stopped"


def test_payload_fits_device_buffer(tmp_path):
    # The firmware's RX buffer is 512 bytes, worst case: every field at its limit
    # and full of JSON-escaped quotes.
    big = {"st": "Playing", "ti": '"' * 120, "ar": '"' * 80, "al": '"' * 80,
           "len": 99999, "pos": 99999}
    out = tmp_path / "np.json"
    mw.write_atomic(str(out), big)
    text = out.read_text().rstrip("\n")
    assert len(text.encode()) <= mw.MAX_PAYLOAD
    got = json.loads(text)["np"]
    assert got["st"] == "Playing" and got["len"] == 99999
    assert got["ti"].startswith('"') and len(got["ti"]) < 120   # trimmed, not dropped


def test_normal_payload_untouched():
    p = {"st": "Paused", "ti": "Song", "ar": "Artist", "al": "", "len": 10, "pos": 1}
    assert json.loads(mw.encode(p)) == {"np": p}
