cmake_minimum_required(VERSION 3.18)
project(bench-godot-guest)
set(DOUBLE_PRECISION ON CACHE BOOL "" FORCE)  # the editor builds here are double precision
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../deps/godot-sandbox/program/cpp/cmake godot-sandbox-api)
add_sandbox_program(bench_guest.elf "program.cpp")
