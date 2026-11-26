import pathlib
import subprocess
import sys
import tempfile
import wave


def run_console(executable, folder, commands):
    result = subprocess.run(
        [executable, "--transport", str(folder)], input=commands,
        capture_output=True, text=True, timeout=10, check=True,
    )
    return result.stdout, result.stderr


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    with tempfile.TemporaryDirectory(prefix="playset-console-") as temporary:
        folder = pathlib.Path(temporary)
        for name in ("A", "B"):
            with wave.open(str(folder / f"{name}.wav"), "wb") as stem:
                stem.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
                stem.writeframes(b"\x00\x20" * 48000)
        rejected = subprocess.run(
            [sys.argv[1], "--play", str(folder)], capture_output=True, text=True,
            timeout=10,
        )
        require(rejected.returncode == 1, "Removed playback command was accepted")
        require(not rejected.stdout, "Removed command loaded stems before rejection")
        require(rejected.stderr == "Usage: PlaySet --inspect|--transport <stem-folder>\n",
                "Usage still advertises the removed command")
        inspected = subprocess.run(
            [sys.argv[1], "--inspect", str(folder)], capture_output=True, text=True,
            timeout=10, check=True,
        )
        require(not inspected.stderr and "Prepared 2 stems" in inspected.stdout,
                "Stem inspection failed")
        require("Commands:" not in inspected.stdout, "Inspection opened the console")
        commands = (
            "\nhelp\nvolume 0 0.5\nmute 0 on\nmutes 0 1 on\nmute 0 off\n"
            "solo 0 on\nsoli 1 on\nsolo 0 off\nplay\npause\nseek 0.25\n"
            "status\ntracks\nquit\nvolume 0 1\n"
        )
        output, errors = run_console(sys.argv[1], folder, commands)
        require(not errors, f"Valid commands failed: {errors}")
        require("0: A volume=0.5 mute=off solo=off" in output, "Wrong first-track state")
        require("1: B volume=1 mute=on solo=on" in output, "Additive Soli or mute failed")
        require("paused: 0.25 / 1 seconds" in output, "Paused seek lost position")
        require(output.count("Commands:") == 2, "Help did not show the command list again")
        require("volume=1 mute=off solo=off" not in output, "Commands ran after quit")

        baseline = "volume 0 0.5\nmute 1 on\nsolo 1 on\nseek 0.25\n"
        invalid_commands = [
            "mute 0", "mute 0 maybe", "solo 0 on extra", "mute -1 on", "mute 1x on",
            "mute 184467440737095516160 on", "mutes on", "mutes 0 99 on",
            "soli 0 99 on", "soli 0 -1 on", "soli 0 maybe", "volume 0 0.5x",
            "volume 0 2", "seek 1x", "seek 5", "tracks extra", "play extra",
            "quit extra", "unknown", "soli " + "0 " * 257 + "on",
        ]
        invalid_script = baseline + "".join(
            invalid + "\ntracks\nstatus\n" for invalid in invalid_commands
        ) + "quit\n"
        output, errors = run_console(sys.argv[1], folder, invalid_script)
        require(errors.count("PlaySet:") == len(invalid_commands),
                "An invalid command was accepted or stopped subsequent commands")
        require(output.count("0: A volume=0.5 mute=off solo=off") == len(invalid_commands),
                "A rejected command partly changed the first track")
        require(output.count("1: B volume=1 mute=on solo=on") == len(invalid_commands),
                "A rejected command changed the existing selection")
        require(output.count("stopped: 0.25 / 1 seconds") >= len(invalid_commands),
                "Rejected commands changed the transport state")
        output, errors = run_console(
            sys.argv[1], folder,
            "mutes 0 1 on\nmutes 0 1 off\nsoli 0 1 on\nsoli 0 1 off\n"
            "solo 0 on\nsolo 1 on\ntracks\nquit\n",
        )
        require(not errors, errors)
        require("0: A volume=1 mute=off solo=off" in output, "Exclusive Solo did not clear old selection")
        require("1: B volume=1 mute=off solo=on" in output, "Batch disable or exclusive Solo failed")
    print("Real main.cpp console commands and input rejection passed without an audio device.")


if __name__ == "__main__":
    main()
