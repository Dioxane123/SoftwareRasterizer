cmake_minimum_required(VERSION 3.16)
project(SoftwareRasterizer LANGUAGES C CXX)

find_package(Eigen3 3.3 REQUIRED NO_MODULE)
find_package(SDL3 3.2 QUIET CONFIG)

option(SOFTWARE_RASTERIZER_FETCH_SDL3 "Download SDL3 if no installed package is found" ON)
if(NOT SDL3_FOUND)
    if(NOT SOFTWARE_RASTERIZER_FETCH_SDL3)
        message(FATAL_ERROR "SDL3 >= 3.2 was not found. Set SDL3_DIR or CMAKE_PREFIX_PATH, or enable SOFTWARE_RASTERIZER_FETCH_SDL3.")
    endif()

    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    # Build a static SDL by default so the downloaded dependency needs no DLL copy.
    set(SDL_SHARED OFF CACHE BOOL "Build SDL as a shared library")
    set(SDL_STATIC ON CACHE BOOL "Build SDL as a static library")
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build the SDL test library")
    set(SDL_TESTS OFF CACHE BOOL "Build SDL tests")
    set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL examples")
    FetchContent_Declare(SDL3
        URL https://libsdl.org/release/SDL3-3.4.16.tar.gz
        URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
    )
    FetchContent_MakeAvailable(SDL3)
endif()

add_executable(software_rasterizer main.cpp rasterizer.cpp triangle.cpp sdl_window.cpp)
target_compile_features(software_rasterizer PRIVATE cxx_std_17)
set_target_properties(software_rasterizer PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(software_rasterizer PRIVATE Eigen3::Eigen SDL3::SDL3)

if(MSVC)
    target_compile_options(software_rasterizer PRIVATE /W4 /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(software_rasterizer PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Installed SDL packages may use a DLL; keep it beside the executable on Windows.
get_target_property(sdl3_library_type SDL3::SDL3 TYPE)
if(WIN32 AND sdl3_library_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET software_rasterizer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:software_rasterizer>"
        VERBATIM
    )
endif()
