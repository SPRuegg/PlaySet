"""Generate synthetic AAC and ALAC M4A tones using macOS afconvert."""

import argparse
import math
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--afconvert", required=True)
parser.add_argument("--output-dir", required=True, type=Path)
parser.add_argument("--frames", type=int, default=11025)
parser.add_argument("--frequency", type=int, default=440)
parser.add_argument("--keep-source", action="store_true")
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory(dir=args.output_dir) as directory:
    wav = Path(directory) / "tone.wav"
    with wave.open(str(wav), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(44100)
        audio.writeframes(
            b"".join(
                struct.pack(
                    "<h", round(8192 * math.sin(2 * math.pi * args.frequency * frame / 44100))
                )
                for frame in range(args.frames)
            )
        )
    for codec, filename in [("aac", "tone.m4a"), ("alac", "tone-alac.m4a")]:
        output = Path(directory) / filename
        options = ["-b", "128000"] if codec == "aac" else []
        subprocess.run(
            [
                args.afconvert,
                "-f",
                "m4af",
                "-d",
                codec,
                *options,
                str(wav),
                str(output),
            ],
            check=True,
            timeout=30,
        )
        output.replace(args.output_dir / filename)
    if args.keep_source:
        wav.replace(args.output_dir / "tone.wav")
