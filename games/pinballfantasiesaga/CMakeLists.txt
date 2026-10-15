cmake_minimum_required(VERSION 3.24)
project(pinballfantasiesaga C CXX)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/amigarecomp.cmake)
# An A1200: the game asks for the AGA chipset and Kickstart 3 (docs/format.md, "Machine").
amiga_add_game(pinballfantasiesaga A1200)
