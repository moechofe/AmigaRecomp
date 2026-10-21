cmake_minimum_required(VERSION 3.24)
project(superfrog C CXX)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/amigarecomp.cmake)
# The loader checks each sector while the disk DMA runs (docs/format.md, "Boot chain").
amiga_add_game(superfrog DISK_TIMED)
