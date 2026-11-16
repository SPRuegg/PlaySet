cmake_minimum_required(VERSION 3.25)

project(PlaySet LANGUAGES CXX)

add_executable(PlaySet main.cpp)
target_compile_features(PlaySet PRIVATE cxx_std_20)
set_target_properties(PlaySet PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(PlaySet PRIVATE /W4)
else()
    target_compile_options(PlaySet PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
