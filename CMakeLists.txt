cmake_minimum_required(VERSION 3.24)
project(ImpulseJIT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

add_subdirectory(ir)
add_subdirectory(frontend)
add_subdirectory(runtime)
add_subdirectory(jit)
add_subdirectory(tests)
add_subdirectory(tools/cpp-cli)
