#include "playback/transport/transport.hpp"

#include <exception>
#include <charconv>
#include <iostream>
#include <string_view>
#include <sstream>
#include <string>
#include <utility>

int main(int argc, char* argv[]) {
    if (argc == 1) {
        std::cout << "Hello PlaySet!\n";
        return 0;
    }
    const auto command = std::string_view(argv[1]);
    if (argc != 3 || (command != "--inspect" && command != "--play" && command != "--transport")) {
        std::cerr << "Usage: PlaySet --inspect|--play|--transport <stem-folder>\n";
        return 1;
    }
    try {
        auto song = playset::prepare_stems(argv[2]);
        std::cout << "Prepared " << song.stems.size() << " stems at " << song.sample_rate
                  << " Hz; duration " << song.duration_seconds << " seconds.\n";
        for (const auto& stem : song.stems) {
            const auto& info = stem.info();
            std::cout << info.path.filename().string() << ": " << info.channels << " channels, "
                      << info.sample_rate << " Hz, " << info.frames << " source frames\n";
        }
        if (command == "--transport") {
            playset::Transport transport(std::move(song));
            std::cout << "Commands: play, pause, stop, seek <seconds>, tracks, volume <track-id> <0..1>, status, quit\n";
            for (std::string line; std::cout << "> " && std::getline(std::cin, line);) {
                try {
                    std::istringstream input(line);
                    std::string action, extra;
                    input >> action;
                    if (action == "quit") break;
                    if (action == "volume") {
                        std::string id_text;
                        playset::TrackId id;
                        double volume;
                        if (!(input >> id_text >> volume) || input >> extra)
                            throw std::invalid_argument("Use volume <track-id> <0..1>.");
                        const auto [end, error] = std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
                        if (error != std::errc{} || end != id_text.data() + id_text.size())
                            throw std::invalid_argument("Track ID must be a nonnegative integer from tracks.");
                        transport.set_track_volume(id, volume);
                    } else if (action == "tracks") {
                        if (input >> extra) throw std::invalid_argument("Unexpected argument.");
                        for (const auto &track : transport.tracks())
                            std::cout << track.id << ": " << track.name << " volume=" << track.volume << '\n';
                    } else if (action == "seek") {
                        double seconds;
                        if (!(input >> seconds) || input >> extra)
                            throw std::invalid_argument("Use seek <seconds>.");
                        transport.seek(seconds);
                    } else {
                        if (input >> extra) throw std::invalid_argument("Unexpected argument.");
                        if (action == "play") transport.play();
                        else if (action == "pause") transport.pause();
                        else if (action == "stop") transport.stop();
                        else if (action != "status") throw std::invalid_argument("Unknown command.");
                    }
                    const auto state = transport.snapshot();
                    const char *names[] = {"stopped", "playing", "paused", "ended"};
                    std::cout << names[static_cast<unsigned>(state.status)] << ": "
                              << state.position_seconds << " / " << state.duration_seconds << " seconds\n";
                } catch (const std::exception &error) {
                    std::cerr << "PlaySet: " << error.what() << '\n';
                }
            }
        } else if (command == "--play") {
            std::cout << "Playing through the default audio output..." << std::endl;
            const auto report = playset::play_song(std::move(song));
            std::cout << "Finished: " << report.rendered_frames << " rendered source frames; " << report.clipped_samples
                      << " clipped samples; " << report.underruns << " buffer underruns.\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "PlaySet: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
