
cmake_minimum_required(VERSION 3.22)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

project(fiya C CXX)

find_package(Threads REQUIRED)

add_library(fiya SHARED fiya.cpp fiya-cyg-overloads.cpp)
target_compile_features(fiya PRIVATE cxx_std_20)
target_include_directories(fiya PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(fiya PRIVATE -fvisibility=hidden -fno-instrument-functions)
target_compile_options(fiya PRIVATE -gdwarf -g3)
target_link_libraries(fiya PUBLIC Threads::Threads)

