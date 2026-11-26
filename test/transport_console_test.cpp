#include "console/transport_console.hpp"
#include "fixture_support.hpp"
#include "playback/transport/transport.hpp"

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  try {
    playset::test::TempFolder folder;
    playset::test::write_pcm_wav(folder.path / "Bass.wav", 48000, 1, 16, 48000);
    playset::Transport transport(playset::prepare_stems(folder.path));
    std::istringstream commands("mute 0 maybe\nvolume 0 0.5\nmute 0 on\ntracks\n");
    std::ostringstream output, errors;
    playset::run_transport_console(transport, commands, output, errors);
    require(commands.eof(), "Console did not return when input ended");
    require(errors.str() == "PlaySet: Choose on or off.\n", "Command error used the wrong stream");
    require(output.str().find("PlaySet:") == std::string::npos, "Error leaked into normal output");
    require(output.str().find("0: Bass volume=0.5 mute=on solo=off") != std::string::npos,
            "Console did not continue after a rejected command");
    require(transport.tracks()[0].muted, "Console did not update the supplied transport");

    std::istringstream next_commands("mute 0 off\nquit\nvolume 0 1\n");
    std::ostringstream next_output, next_errors;
    playset::run_transport_console(transport, next_commands, next_output, next_errors);
    require(next_errors.str().empty(), "Second console invocation failed");
    const auto track = transport.tracks()[0];
    require(!track.muted && track.volume == 0.5, "Quit reset settings or ran a later command");
    std::string remaining;
    std::getline(next_commands, remaining);
    require(remaining == "volume 0 1", "Quit consumed commands beyond its own line");
    require(next_output.str().find("Commands:") != std::string::npos,
            "Second invocation did not write help to its supplied output");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
