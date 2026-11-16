#include "playback/audio_output.hpp"

#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

int main(int argc, char* argv[]) {
    if (argc == 1) {
        std::cout << "Hello PlaySet!\n";
        return 0;
    }
    const auto command = std::string_view(argv[1]);
    if (argc != 3 || (command != "--inspect" && command != "--play")) {
        std::cerr << "Usage: PlaySet --inspect|--play <stem-folder>\n";
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
        if (command == "--play") {
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
