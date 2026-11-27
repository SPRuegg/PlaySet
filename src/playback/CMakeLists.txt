add_library(playset_playback STATIC
  mixing/mixer_controls.cpp
  buffering/stem_block.cpp
  buffering/stem_block_queue.cpp
  transport/transport.cpp
  buffering/buffered_playback.cpp
  output/macos/audio_output.cpp
)
add_library(PlaySet::Playback ALIAS playset_playback)
target_include_directories(playset_playback PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(playset_playback PUBLIC cxx_std_20)
set_target_properties(playset_playback PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(playset_playback PRIVATE -Wall -Wextra -Wpedantic)
find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
target_link_libraries(playset_playback PUBLIC PlaySet::Audio PRIVATE "${AUDIOTOOLBOX_FRAMEWORK}")
