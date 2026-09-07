cmake_minimum_required(VERSION 3.16)
project(SoftwareRasterizer LANGUAGES CXX)

find_package(Eigen3 3.3 REQUIRED NO_MODULE)
find_package(X11 REQUIRED)

add_executable(software_rasterizer main.cpp rasterizer.cpp triangle.cpp)
target_compile_features(software_rasterizer PRIVATE cxx_std_17)
set_target_properties(software_rasterizer PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(software_rasterizer PRIVATE Eigen3::Eigen X11::X11)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(software_rasterizer PRIVATE -Wall -Wextra -Wpedantic)
endif()
