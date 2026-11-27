import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import wave


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(*arguments):
    return subprocess.run(
        [sys.argv[1], *map(str, arguments)], capture_output=True, text=True, timeout=15,
    )


def inspect(folder, included, excluded):
    result = run("--fingerprint", folder)
    require(result.returncode == 0 and not result.stderr, result.stderr)
    lines = result.stdout.splitlines()
    match = re.fullmatch(r"Stem collection fingerprint v1: ([0-9a-f]{64})", lines[0])
    require(match is not None, "Missing collection digest or version")
    require(lines[1] == f"Included stems: {included}", "Wrong included count")
    require(lines[2] == f"Excluded references: {excluded}", "Wrong reference count")
    require(lines[3] == "Individual fingerprint version: 1", "Missing individual version")
    records = {}
    for line in lines[4:]:
        record = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        require(record is not None, "Invalid individual fingerprint record")
        records[record[2]] = record[1]
    require(len(records) == included, "Missing source records")
    require("Prepared " not in result.stdout and "Commands:" not in result.stdout,
            "Fingerprint inspection entered playback startup")
    return match[1], records


def write_wav(path, sample, rate=8000):
    with wave.open(str(path), "wb") as audio:
        audio.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        audio.writeframes(sample.to_bytes(2, "little", signed=True) * rate)


def main():
    fixtures = pathlib.Path(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="playset-fingerprint-console-") as temporary:
        folder = pathlib.Path(temporary)
        for arguments in [("--fingerprint",), ("--fingerprint", folder, "extra")]:
            result = run(*arguments)
            require(result.returncode == 1 and not result.stdout and "Usage:" in result.stderr,
                    "Invalid argument count was accepted")
        for path in [folder, folder / "missing"]:
            result = run("--fingerprint", path)
            require(result.returncode == 1 and not result.stdout and "PlaySet:" in result.stderr,
                    "Invalid folder returned a partial result")

        write_wav(folder / "Bass.WAV", 8192)
        write_wav(folder / "Piano.wav", 4096, 44100)
        (folder / "Guide.wav").write_text("custom reference")
        (folder / "notes.txt").write_text("not audio")
        (folder / "ignored.wav").mkdir()
        digest, records = inspect(folder, 2, 1)
        require(records["Bass.WAV"] != records["Piano.wav"], "Individual source records lost identity")
        require(inspect(folder, 2, 1) == (digest, records), "Repeated inspection changed results")

        (folder / "Bass.WAV").rename(folder / "Z Bass.wav")
        (folder / "Piano.wav").rename(folder / "A Piano.wav")
        renamed, renamed_records = inspect(folder, 2, 1)
        require(renamed == digest and renamed_records["Z Bass.wav"] == records["Bass.WAV"] and
                renamed_records["A Piano.wav"] == records["Piano.wav"],
                "Renaming or reordering lost identity or source association")
        shutil.copyfile(folder / "Z Bass.wav", folder / "Another Bass.wav")
        require(inspect(folder, 3, 1)[0] != digest, "Copied audio was removed from the collection")

        (folder / "broken.wav").write_text("corrupt included audio")
        result = run("--fingerprint", folder)
        require(result.returncode == 1 and not result.stdout and "PlaySet:" in result.stderr,
                "Failed calculation printed a partial fingerprint")

        compressed = folder / "compressed"
        compressed.mkdir()
        for source, name in [("tone.m4a", "Bass.m4a"), ("tone-alac.m4a", "Piano.M4A")]:
            shutil.copyfile(fixtures / source, compressed / name)
        require(inspect(compressed, 2, 0) == inspect(compressed, 2, 0),
                "Compressed command results changed between processes")
        write_wav(compressed / "Additional.wav", 4096)
        inspect(compressed, 3, 0)

        references = folder / "references"
        references.mkdir()
        (references / "Click.wav").write_text("custom click")
        result = run("--fingerprint", references)
        require(result.returncode == 1 and not result.stdout and "non-reference" in result.stderr,
                "Reference-only folder was accepted")

        oversized = folder / "oversized"
        oversized.mkdir()
        for index in range(257):
            (oversized / f"Stem {index}.wav").touch()
        result = run("--fingerprint", oversized)
        require(result.returncode == 1 and not result.stdout and "256" in result.stderr,
                "Oversized folder was decoded before rejecting its file count")


if __name__ == "__main__":
    main()
