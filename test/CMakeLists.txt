add_test(
    NAME playset.startup
    COMMAND "${CMAKE_COMMAND}"
        "-DPLAYSET_EXECUTABLE=$<TARGET_FILE:PlaySet>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_startup.cmake"
)
set_tests_properties(playset.startup PROPERTIES TIMEOUT 10)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(AFCONVERT_EXECUTABLE NAMES afconvert REQUIRED)
set(M4A_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/tone.m4a")
set(ALAC_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/tone-alac.m4a")
add_custom_command(
    OUTPUT "${M4A_FIXTURE}" "${ALAC_FIXTURE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_m4a_fixture.py"
        --afconvert "${AFCONVERT_EXECUTABLE}" --output-dir "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generate_m4a_fixture.py" "${AFCONVERT_EXECUTABLE}"
    COMMENT "Generating synthetic M4A test fixture"
    VERBATIM
)
add_custom_target(playset_test_fixtures DEPENDS "${M4A_FIXTURE}" "${ALAC_FIXTURE}")

add_executable(stem_loader_test stem_loader_test.cpp)
add_dependencies(stem_loader_test playset_test_fixtures)
target_link_libraries(stem_loader_test PRIVATE PlaySet::Playback)
set_target_properties(stem_loader_test PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(stem_loader_test PRIVATE -Wall -Wextra -Wpedantic)
foreach(case IN ITEMS pcm sample_rates m4a invalid)
    add_test(NAME stems.${case} COMMAND stem_loader_test "${case}" "${M4A_FIXTURE}")
    set_tests_properties(stems.${case} PROPERTIES TIMEOUT 15)
endforeach()

add_test(NAME stems.alac COMMAND stem_loader_test m4a "${ALAC_FIXTURE}")
set_tests_properties(stems.alac PROPERTIES TIMEOUT 15)

add_executable(playback_test playback_test.cpp)
add_dependencies(playback_test playset_test_fixtures)
target_link_libraries(playback_test PRIVATE PlaySet::Playback)
set_target_properties(playback_test PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(playback_test PRIVATE -Wall -Wextra -Wpedantic)
foreach(case IN ITEMS mixing decoding queue concurrent buffered error)
  add_test(NAME playback.${case} COMMAND playback_test "${case}" "${M4A_FIXTURE}")
  set_tests_properties(playback.${case} PROPERTIES TIMEOUT 15)
endforeach()

add_executable(transport_test transport_test.cpp)
add_dependencies(transport_test playset_test_fixtures)
target_link_libraries(transport_test PRIVATE PlaySet::Playback)
set_target_properties(transport_test PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(transport_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME playback.transport COMMAND transport_test "${M4A_FIXTURE}" "${ALAC_FIXTURE}")
set_tests_properties(playback.transport PROPERTIES TIMEOUT 30)

add_executable(volume_test volume_test.cpp)
target_link_libraries(volume_test PRIVATE PlaySet::Playback)
set_target_properties(volume_test PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(volume_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME playback.volume COMMAND volume_test)
set_tests_properties(playback.volume PROPERTIES TIMEOUT 30)

option(PLAYSET_TEST_AUDIO_OUTPUT "Test the native output device with its volume forced to zero" OFF)
if(PLAYSET_TEST_AUDIO_OUTPUT)
  add_executable(native_output_test native_output_test.cpp
    "${PROJECT_SOURCE_DIR}/src/playback/output/macos/audio_output.cpp")
  target_link_libraries(native_output_test PRIVATE PlaySet::Playback)
  set_target_properties(native_output_test PROPERTIES CXX_EXTENSIONS OFF)
  target_compile_options(native_output_test PRIVATE -Wall -Wextra -Wpedantic
    -include "${CMAKE_CURRENT_SOURCE_DIR}/muted_audio_output.hpp")
  add_test(NAME playback.native_output COMMAND native_output_test)
  set_tests_properties(playback.native_output PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
endif()
