if(NOT DEFINED PLAYSET_EXECUTABLE)
    message(FATAL_ERROR "PLAYSET_EXECUTABLE is required")
endif()

execute_process(
    COMMAND "${PLAYSET_EXECUTABLE}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
    TIMEOUT 5
)

if(NOT "${exit_code}" STREQUAL "0")
    message(FATAL_ERROR "PlaySet did not exit successfully: ${exit_code}\n${error_output}")
endif()

if(NOT "${output}" STREQUAL "Hello PlaySet!\n")
    message(FATAL_ERROR "Unexpected startup output: [${output}]")
endif()

if(NOT "${error_output}" STREQUAL "")
    message(FATAL_ERROR "Unexpected startup error output: ${error_output}")
endif()
