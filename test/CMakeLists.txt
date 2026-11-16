add_test(
    NAME playset.startup
    COMMAND "${CMAKE_COMMAND}"
        "-DPLAYSET_EXECUTABLE=$<TARGET_FILE:PlaySet>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_startup.cmake"
)
set_tests_properties(playset.startup PROPERTIES TIMEOUT 10)
