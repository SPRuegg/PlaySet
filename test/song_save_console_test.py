import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import time
import wave


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def quote(path):
    return json.dumps(str(path), ensure_ascii=False)


def run(folder, commands):
    result = subprocess.run(
        [sys.argv[1], "--transport", str(folder / "stems")], input=commands,
        cwd=folder, capture_output=True, text=True, timeout=10, check=True,
    )
    return result.stdout, result.stderr


def read(path):
    return json.loads(path.read_text())


def write_stem(path, sample=8192):
    with wave.open(str(path), "wb") as audio:
        audio.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        audio.writeframes(struct.pack("<h", sample) * 8000)


def saving(folder):
    first = folder / 'First "Mix" Ω.plsong'
    second = folder / "Second Mix.plsong"
    output, errors = run(folder, (
        "volume 0 0.5\nmute 0 on\nsolo 0 on\nsoli 1 on\nvolume 2 0\n"
        "master-volume 0.25\nmaster-mute on\n"
        f"save-as {quote(first)}\nvolume 0 0.125\nsave\n"
        f"save-as {quote(second)}\nvolume 1 0.75\nsave\n"
        "save-as relative.plsong\nvolume 1 0.875\nsave\nstatus\nquit\n"
    ))
    require(not errors and output.count("Saved ") == 6, "Valid save commands failed: " + errors)
    original, copied, relative = read(first), read(second), read(folder / "relative.plsong")
    require(original["id"] == copied["id"] == relative["id"], "Saving changed the configuration ID")
    require([t["id"] for t in original["tracks"]] == [t["id"] for t in relative["tracks"]],
            "Repeated saves changed persistent track IDs")
    require(original["stemFolder"] == str((folder / "stems").resolve()), "Wrong source folder was saved")
    require(original["schemaVersion"] == 1 and original["fingerprint"]["stemCount"] == 2,
            "Saved identity or reference exclusions were lost")
    tracks = original["tracks"]
    require(tracks[0]["volume"] == 0.125 and tracks[0]["mute"] and tracks[0]["solo"] and
            tracks[1]["volume"] == 1 and not tracks[1]["mute"] and tracks[1]["solo"],
            "Save did not update stored volumes, mute, or additive solos")
    require(tracks[0]["fingerprint"] == tracks[1]["fingerprint"] and tracks[0]["id"] != tracks[1]["id"],
            "Duplicate audio lost its distinct settings")
    require(tracks[2]["file"] == "Guide.wav" and tracks[2]["fingerprint"] is None and
            tracks[2]["volume"] == 0 and not tracks[2]["mute"], "Reference settings were lost")
    require(original["master"] == {"volume": 0.25, "mute": True}, "Master settings were lost")
    require(copied["tracks"][1]["volume"] == 0.75 and relative["tracks"][1]["volume"] == 0.875,
            "Save did not follow the latest successful Save As destination")
    require("stopped: 0 / 1 seconds; master-volume=0.25 master-mute=on" in output,
            "Saving changed the stopped transport")


def failures(folder):
    first = folder / "Failure Baseline.plsong"
    missing = folder / "missing" / "Unavailable.plsong"
    syntax = ["save-as", 'save-as ""', 'save-as "unterminated',
              f"save-as {quote(first)} extra", "save extra"]
    output, errors = run(folder, (
        "save\n" + "\n".join(syntax) + "\n"
        f"save-as {quote(missing)}\nsave\n"
        f"save-as {quote(first)}\nvolume 0 0.375\n"
        f"save-as {quote(missing)}\nsave-as wrong.wav\nsave\nquit\n"
    ))
    require(errors.count("Choose a destination with save-as first.") == 2,
            "A failed first Save As established a destination")
    require(errors.count("PlaySet:") == 10 and output.count("Saved ") == 2,
            "Invalid commands did not report errors and continue")
    require(read(first)["tracks"][0]["volume"] == 0.375,
            "Failed Save As replaced the remembered successful destination")
    require(not missing.parent.exists() and not (folder / "wrong.wav").exists(),
            "Failed saves created a folder or invalid destination")
    require("PlaySet:" not in output, "Save errors leaked into normal output")


def transport_states(folder):
    stopped = folder / "Stopped.plsong"
    playing = folder / "Playing.plsong"
    paused = folder / "Paused.plsong"
    output, errors = run(folder, (
        f"save-as {quote(stopped)}\nplay\nseek 0.25\n"
        f"save-as {quote(playing)}\nstatus\npause\n"
        f"save-as {quote(paused)}\nstatus\nstop\nsave\nstatus\nquit\n"
    ))
    require(not errors, "Saving changed transport behavior: " + errors)
    for state in ["playing: 0.25 / 1 seconds", "paused: 0.25 / 1 seconds", "stopped: 0 / 1 seconds"]:
        require(output.count(state) >= 2, "Save changed playback state or position: " + state)
    require(read(stopped)["id"] == read(playing)["id"] == read(paused)["id"],
            "Playback operations changed the saved configuration ID")


def changed_sources(folder):
    first = folder / "Verified.plsong"
    second = folder / "Changed.plsong"
    process = subprocess.Popen(
        [sys.argv[1], "--transport", str(folder / "stems")],
        cwd=folder, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    try:
        process.stdin.write(f"save-as {quote(first)}\n")
        process.stdin.flush()
        deadline = time.monotonic() + 5
        while not first.exists():
            require(process.poll() is None and time.monotonic() < deadline,
                    "Initial save never completed")
            time.sleep(0.01)
        original = first.read_bytes()
        replacement = folder / "replacement.wav"
        write_stem(replacement, 4096)
        replacement.replace(folder / "stems" / "Bass.wav")
        output, errors = process.communicate(
            f"volume 0 0.125\nsave\nsave-as {quote(second)}\ntracks\nquit\n", timeout=10,
        )
        require(process.returncode == 0 and errors.count("no longer match") == 2,
                "Save commands did not reuse their source verification baseline")
        require(first.read_bytes() == original and not second.exists(),
                "Mismatched source audio changed an existing file or created a new one")
        require("0: Bass volume=0.125 mute=off solo=off" in output,
                "Console stopped accepting controls after a rejected save")
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate()


def main():
    for case in [saving, failures, transport_states, changed_sources]:
        with tempfile.TemporaryDirectory(prefix="playset-song-save-console-") as temporary:
            folder = pathlib.Path(temporary)
            (folder / "stems").mkdir()
            for name in ["Bass.wav", "Drums.wav", "Guide.wav"]:
                write_stem(folder / "stems" / name)
            case(folder)
            require(not list(folder.rglob(".playset-song-*.tmp")), "A save left a temporary file behind")


if __name__ == "__main__":
    main()
