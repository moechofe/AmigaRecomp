include_guard(GLOBAL)

get_filename_component(AMIGA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

function(amiga_warnings target)
	target_compile_options(${target} PRIVATE -Wall -Wextra)
endfunction()

# --- dependencies ------------------------------------------------------------------------------

if(NOT DEFINED CACHE{FETCHCONTENT_BASE_DIR})
	set(FETCHCONTENT_BASE_DIR "${AMIGA_ROOT}/build/deps" CACHE PATH "")
endif()
include(FetchContent)

# Only the sources are shared between build trees. FetchContent's own sub-build and binary
# directories stay in each tree, so a second tree never re-runs the git clone.
macro(amiga_fetch name repo tag)
	string(TOLOWER "${name}" _lower)
	string(TOUPPER "${name}" _upper)
	set(_src "${FETCHCONTENT_BASE_DIR}/${_lower}-src")
	set(_pin "${FETCHCONTENT_BASE_DIR}/${_lower}.pin")
	set(_want "${repo} ${tag}")
	FetchContent_Declare(${name}
		GIT_REPOSITORY ${repo}
		GIT_TAG ${tag}
		${ARGN}
		SOURCE_DIR "${_src}"
		SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/${_lower}-subbuild"
		BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/${_lower}-build")
	set(_reused OFF)
	if(EXISTS "${_pin}" AND EXISTS "${_src}")
		file(READ "${_pin}" _have)
		if(_have STREQUAL _want)
			set(FETCHCONTENT_SOURCE_DIR_${_upper} "${_src}")
			set(_reused ON)
		endif()
	endif()
	FetchContent_MakeAvailable(${name})
	if(NOT _reused)
		file(WRITE "${_pin}" "${_want}")
	endif()
endmacro()

amiga_fetch(musashi https://github.com/kstenerud/Musashi.git 313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd)

set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
amiga_fetch(SDL3 https://github.com/libsdl-org/SDL.git release-3.4.18 GIT_SHALLOW TRUE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
amiga_fetch(freetype https://github.com/freetype/freetype.git VER-2-14-1 GIT_SHALLOW TRUE)
add_library(Freetype::Freetype ALIAS freetype)

set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Freetype ON)
amiga_fetch(rmlui https://github.com/mikke89/RmlUi.git 6.3 GIT_SHALLOW TRUE)

amiga_fetch(miniz https://github.com/richgel999/miniz.git 3.1.2 GIT_SHALLOW TRUE)

# --- the core ----------------------------------------------------------------------------------

# Musashi generates its opcode table with a host tool.
add_executable(m68kmake "${musashi_SOURCE_DIR}/m68kmake.c")
target_compile_options(m68kmake PRIVATE -w)

set(MUSASHI_GEN "${CMAKE_BINARY_DIR}/musashi-gen")
add_custom_command(
	OUTPUT "${MUSASHI_GEN}/m68kops.c" "${MUSASHI_GEN}/m68kops.h"
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${MUSASHI_GEN}"
	COMMAND m68kmake "${MUSASHI_GEN}" "${musashi_SOURCE_DIR}/m68k_in.c"
	DEPENDS m68kmake "${musashi_SOURCE_DIR}/m68k_in.c"
	VERBATIM)

set(MUSASHI_SOURCES
	"${musashi_SOURCE_DIR}/m68kcpu.c"
	"${musashi_SOURCE_DIR}/m68kdasm.c"
	"${musashi_SOURCE_DIR}/softfloat/softfloat.c"
	"${MUSASHI_GEN}/m68kops.c")
set_source_files_properties(${MUSASHI_SOURCES} PROPERTIES COMPILE_OPTIONS -w)

file(GLOB AMIGA_CORE_SOURCES CONFIGURE_DEPENDS "${AMIGA_ROOT}/core/*.c")
# interp.c includes Musashi's m68kcpu.h for the stop flag and the instruction cycle table.
set_source_files_properties("${AMIGA_ROOT}/core/interp.c" PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable")
add_library(amiga_core STATIC ${AMIGA_CORE_SOURCES} ${MUSASHI_SOURCES})
target_include_directories(amiga_core PUBLIC
	"${AMIGA_ROOT}/core"
	"${musashi_SOURCE_DIR}"
	"${MUSASHI_GEN}")
target_compile_definitions(amiga_core PUBLIC [[MUSASHI_CNF="musashi_conf.h"]])
if(UNIX)
	target_link_libraries(amiga_core PUBLIC m)
endif()
amiga_warnings(amiga_core)

# --- the frontend ------------------------------------------------------------------------------

set(RMLUI_GL3_SOURCE "${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp")
# This is the one translation unit that defines GLAD_GL_IMPLEMENTATION, so gladLoadGL links
# without libGL.
set_source_files_properties("${RMLUI_GL3_SOURCE}" PROPERTIES COMPILE_OPTIONS -w)
file(GLOB AMIGA_FRONTEND_SOURCES CONFIGURE_DEPENDS "${AMIGA_ROOT}/frontend/*.c" "${AMIGA_ROOT}/frontend/*.cpp")
add_library(amiga_frontend STATIC ${AMIGA_FRONTEND_SOURCES} "${RMLUI_GL3_SOURCE}")
target_include_directories(amiga_frontend PUBLIC
	"${AMIGA_ROOT}/frontend"
	"${rmlui_SOURCE_DIR}/Backends")
target_compile_definitions(amiga_frontend PRIVATE RMLUI_SDL_VERSION_MAJOR=3)
target_link_libraries(amiga_frontend PUBLIC amiga_core SDL3::SDL3-static RmlUi::Core miniz ${CMAKE_DL_LIBS})
amiga_warnings(amiga_frontend)

# --- one executable per game -------------------------------------------------------------------

function(amiga_add_game id)
	file(GLOB game_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
	add_executable(${id} ${game_sources})
	target_link_libraries(${id} PRIVATE amiga_frontend)
	set_target_properties(${id} PROPERTIES LINKER_LANGUAGE CXX)
	amiga_warnings(${id})
	if(MINGW)
		target_link_options(${id} PRIVATE -static)
	elseif(UNIX)
		target_link_options(${id} PRIVATE -static-libgcc -static-libstdc++)
	endif()
endfunction()
