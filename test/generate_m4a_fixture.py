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
                    "<h", round(8192 * math.sin(2 * math.pi * 440 * frame / 44100))
                )
                for frame in range(11025)
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
