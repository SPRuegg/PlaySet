include(FetchContent)
set(JSON_BuildTests OFF)
set(JSON_Install OFF)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(playset_songs STATIC song_configuration.cpp song_file.cpp macos/atomic_song_file.cpp)
add_library(PlaySet::Songs ALIAS playset_songs)
target_link_libraries(playset_songs PUBLIC PlaySet::Audio PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(playset_songs PUBLIC cxx_std_20)
set_target_properties(playset_songs PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(playset_songs PRIVATE -Wall -Wextra -Wpedantic)

add_library(playset_song_capture STATIC song_configuration_capture.cpp macos/song_id.cpp)
add_library(PlaySet::SongCapture ALIAS playset_song_capture)
target_link_libraries(playset_song_capture PUBLIC PlaySet::Songs PRIVATE PlaySet::Playback)
set_target_properties(playset_song_capture PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(playset_song_capture PRIVATE -Wall -Wextra -Wpedantic)
