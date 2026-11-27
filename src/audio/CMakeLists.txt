if(NOT APPLE)
  message(FATAL_ERROR "Stem decoding currently requires macOS.")
endif()

add_library(playset_audio STATIC
  loading/stem_loader.cpp
  loading/macos/stem_reader.cpp
  fingerprinting/stem_fingerprint.cpp
  fingerprinting/stem_collection.cpp
  fingerprinting/macos/m4a_packets.cpp
  fingerprinting/macos/sha256.cpp
  fingerprinting/macos/fingerprint_source.cpp
)
add_library(PlaySet::Audio ALIAS playset_audio)
target_include_directories(playset_audio PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(playset_audio PUBLIC cxx_std_20)
set_target_properties(playset_audio PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(playset_audio PRIVATE -Wall -Wextra -Wpedantic)
find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
target_link_libraries(playset_audio PRIVATE "${AUDIOTOOLBOX_FRAMEWORK}" "${COREFOUNDATION_FRAMEWORK}")
