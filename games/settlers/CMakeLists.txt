cmake_minimum_required(VERSION 3.24)
project(settlers C CXX)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/amigarecomp.cmake)
# The game loads its music only with 1 MB of chip RAM (docs/format.md, "Memory").
amiga_add_game(settlers CHIP_1MB)
