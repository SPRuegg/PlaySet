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
