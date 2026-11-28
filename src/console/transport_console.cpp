#include "transport_console.hpp"

#include "playback/transport/transport.hpp"
#include "songs/song_configuration_capture.hpp"
#include "songs/song_file.hpp"

#include <charconv>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace playset {
namespace {
TrackId parse_track_id(std::string_view text) {
  TrackId id;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("Track ID must be a nonnegative integer from tracks.");
  return id;
}
bool parse_enabled(std::string_view text) {
  if (text != "on" && text != "off") throw std::invalid_argument("Choose on or off.");
  return text == "on";
}
} // namespace

void run_transport_console(Transport &transport, std::istream &commands,
                           std::ostream &output, std::ostream &errors) {
  const char *help =
      "Commands: play, pause, stop, seek <seconds>, tracks, volume <track-id> <0..1>, status, help, quit\n"
      "  mute <track-id> <on|off>\n"
      "  mutes <track-id>... <on|off>\n"
      "  solo <track-id> <on|off> (exclusive when enabled)\n"
      "  soli <track-id>... <on|off> (additive)\n"
      "  master-volume <0..1>\n"
      "  master-mute <on|off>\n"
      "  save-as <path> (quote paths containing spaces; include .plsong)\n"
      "  save (update the last successful destination)\n";
  std::unique_ptr<SongConfigurationCapture> capture;
  std::optional<std::filesystem::path> destination;
  output << help;
  for (std::string line; output << "> " && std::getline(commands, line);) {
    try {
      std::istringstream input(line);
      std::string action, extra;
      input >> action;
      if (action.empty()) continue;
      if (action == "quit") {
        if (input >> extra) throw std::invalid_argument("Unexpected argument.");
        break;
      }
      if (action == "save" || action == "save-as") {
        std::filesystem::path candidate;
        if (action == "save-as") {
          std::string path;
          if (!(input >> std::quoted(path)) || path.empty() || input >> extra)
            throw std::invalid_argument("Use save-as <path>; quote paths containing spaces.");
          candidate = std::filesystem::absolute(path);
        } else {
          if (input >> extra) throw std::invalid_argument("Use save without arguments.");
          if (!destination) throw std::invalid_argument("Choose a destination with save-as first.");
          candidate = *destination;
        }
        if (!capture)
          capture = std::make_unique<SongConfigurationCapture>(
              transport, transport.stems().front().path.parent_path());
        const auto song = capture->capture();
        save_song_configuration(candidate, song);
        destination = std::move(candidate);
        output << "Saved " << std::quoted(destination->string()) << '\n';
        continue;
      }
      if (action == "mute" || action == "mutes" || action == "solo" || action == "soli") {
        std::vector<std::string> arguments;
        for (std::string argument; input >> argument;) arguments.push_back(argument);
        const bool batch = action == "mutes" || action == "soli";
        if (arguments.size() < 2 || arguments.size() > maximum_tracks + 1 ||
            (!batch && arguments.size() != 2))
          throw std::invalid_argument("Supply track IDs followed by on or off; mute and solo take one ID.");
        const bool enabled = parse_enabled(arguments.back());
        std::vector<TrackId> ids;
        ids.reserve(arguments.size() - 1);
        for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
          const auto &text = arguments[i];
          ids.push_back(parse_track_id(text));
        }
        if (action == "mute") transport.set_track_muted(ids.front(), enabled);
        else if (action == "mutes") transport.set_tracks_muted(ids, enabled);
        else if (action == "solo") transport.set_track_solo(ids.front(), enabled);
        else transport.set_tracks_soli(ids, enabled);
      } else if (action == "master-volume") {
        double volume;
        if (!(input >> volume) || input >> extra)
          throw std::invalid_argument("Use master-volume <0..1>.");
        transport.set_master_volume(volume);
      } else if (action == "master-mute") {
        std::string flag;
        if (!(input >> flag) || input >> extra)
          throw std::invalid_argument("Use master-mute <on|off>.");
        transport.set_master_muted(parse_enabled(flag));
      } else if (action == "volume") {
        std::string id_text;
        double volume;
        if (!(input >> id_text >> volume) || input >> extra)
          throw std::invalid_argument("Use volume <track-id> <0..1>.");
        transport.set_track_volume(parse_track_id(id_text), volume);
      } else if (action == "tracks") {
        if (input >> extra) throw std::invalid_argument("Unexpected argument.");
        for (const auto &track : transport.tracks())
          output << track.id << ": " << track.name << " volume=" << track.volume
                 << " mute=" << (track.muted ? "on" : "off")
                 << " solo=" << (track.solo ? "on" : "off") << '\n';
      } else if (action == "seek") {
        double seconds;
        if (!(input >> seconds) || input >> extra)
          throw std::invalid_argument("Use seek <seconds>.");
        transport.seek(seconds);
      } else {
        if (input >> extra) throw std::invalid_argument("Unexpected argument.");
        if (action == "help") { output << help; continue; }
        if (action == "play") transport.play();
        else if (action == "pause") transport.pause();
        else if (action == "stop") transport.stop();
        else if (action != "status") throw std::invalid_argument("Unknown command.");
      }
      const auto state = transport.snapshot();
      const char *names[] = {"stopped", "playing", "paused", "ended"};
      output << names[static_cast<unsigned>(state.status)] << ": "
             << state.position_seconds << " / " << state.duration_seconds << " seconds; "
             << "master-volume=" << state.master_volume
             << " master-mute=" << (state.master_muted ? "on" : "off") << '\n';
    } catch (const std::exception &error) {
      errors << "PlaySet: " << error.what() << '\n';
    }
  }
}

} // namespace playset
