#include "playback/stem_loader.hpp"

#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    if (argc == 1) {
        std::cout << "Hello PlaySet!\n";
        return 0;
    }
    if (argc != 3 || std::string_view(argv[1]) != "--inspect") {
        std::cerr << "Usage: PlaySet --inspect <stem-folder>\n";
        return 1;
    }
    try {
        const auto song = playset::prepare_stems(argv[2]);
        std::cout << "Prepared " << song.stems.size() << " stems at " << song.sample_rate
                  << " Hz; duration " << song.duration_seconds << " seconds.\n";
        for (const auto& stem : song.stems) {
            const auto& info = stem.info();
            std::cout << info.path.filename().string() << ": " << info.channels << " channels, "
                      << info.sample_rate << " Hz, " << info.frames << " source frames\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "Could not prepare stems: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
