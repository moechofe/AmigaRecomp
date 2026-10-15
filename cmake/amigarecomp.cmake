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

# windres compiles the Windows executable's icon resource (amiga_add_game).
if(WIN32)
	enable_language(RC)
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
include("${CMAKE_CURRENT_LIST_DIR}/dumps.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/embed.cmake")

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
set_source_files_properties("${RMLUI_GL3_SOURCE}" "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp" PROPERTIES COMPILE_OPTIONS -w)
file(GLOB AMIGA_FRONTEND_SOURCES CONFIGURE_DEPENDS "${AMIGA_ROOT}/frontend/*.c" "${AMIGA_ROOT}/frontend/*.cpp")
# main.c is compiled into each executable: on MinGW it defines only SDL's WinMain, which nothing
# references before libmingw32, so the linker would never pull it out of an archive.
set(AMIGA_FRONTEND_MAIN "${AMIGA_ROOT}/frontend/main.c")
list(REMOVE_ITEM AMIGA_FRONTEND_SOURCES "${AMIGA_FRONTEND_MAIN}")
file(GLOB AMIGA_MENU_SHARED CONFIGURE_DEPENDS "${AMIGA_ROOT}/frontend/menu/*")
list(APPEND AMIGA_MENU_SHARED
	"${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf"
	"${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Bold.ttf")
amiga_embed("${CMAKE_BINARY_DIR}/menu_shared.c" sdl_menu_shared ${AMIGA_MENU_SHARED})
add_library(amiga_frontend STATIC ${AMIGA_FRONTEND_SOURCES} "${RMLUI_GL3_SOURCE}" "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp"
	"${CMAKE_BINARY_DIR}/menu_shared.c")
target_include_directories(amiga_frontend PUBLIC
	"${AMIGA_ROOT}/frontend"
	"${rmlui_SOURCE_DIR}/Backends")
target_compile_definitions(amiga_frontend PRIVATE RMLUI_SDL_VERSION_MAJOR=3)
target_link_libraries(amiga_frontend PUBLIC amiga_core SDL3::SDL3-static RmlUi::Core miniz ${CMAKE_DL_LIBS})
amiga_warnings(amiga_frontend)

# --- one executable per game -------------------------------------------------------------------

# amiga_add_game(<id> [CHIP_1MB | A1200]): CHIP_1MB builds the machine, core and tests included,
# with 1 MB of chip RAM behind the ECS Agnus instead of 512 KB behind the OCS one. A1200 builds
# an A1200 instead: a 68EC020, the AGA chipset and 2 MB of chip RAM, with Kickstart 3.0's HLE.
function(amiga_add_game id)
	cmake_parse_arguments(PARSE_ARGV 1 game "CHIP_1MB;A1200" "" "")
	if(game_CHIP_1MB AND game_A1200)
		message(FATAL_ERROR "amiga_add_game(${id}): CHIP_1MB and A1200 exclude each other")
	endif()
	if(game_CHIP_1MB)
		target_compile_definitions(amiga_core PUBLIC AMIGA_CHIP_SIZE=0x100000u)
	endif()
	if(game_A1200)
		target_compile_definitions(amiga_core PUBLIC AMIGA_A1200=1 AMIGA_CHIP_SIZE=0x200000u)
	endif()
	if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dumps.txt")
		message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/dumps.txt is missing (docs/game.md, item 4)")
	endif()
	amiga_dumps("${CMAKE_CURRENT_SOURCE_DIR}/dumps.txt" "${CMAKE_CURRENT_BINARY_DIR}/game_dumps.c")
	file(GLOB game_menu CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/menu/*")
	amiga_embed("${CMAKE_CURRENT_BINARY_DIR}/game_menu.c" sdl_menu_game ${game_menu})
	file(GLOB game_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
	# Windows shows the first icon resource in Explorer, and SDL gives it to the window.
	set(game_icon "${CMAKE_CURRENT_SOURCE_DIR}/packaging/${id}.ico")
	if(WIN32 AND EXISTS "${game_icon}")
		set(game_rc "${CMAKE_CURRENT_BINARY_DIR}/game_icon.rc")
		file(CONFIGURE OUTPUT "${game_rc}" CONTENT "1 ICON \"${game_icon}\"\n")
		set_source_files_properties("${game_rc}" PROPERTIES OBJECT_DEPENDS "${game_icon}")
		list(APPEND game_sources "${game_rc}")
	endif()
	# Elsewhere the PNG becomes the window icon, and with the .desktop file it makes the
	# applications-menu entry (frontend/desktop.c).
	if(NOT WIN32)
		file(GLOB game_packaging CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/${id}.png"
			"${CMAKE_CURRENT_SOURCE_DIR}/packaging/${id}.desktop")
		amiga_embed("${CMAKE_CURRENT_BINARY_DIR}/game_packaging.c" sdl_packaging ${game_packaging})
		list(APPEND game_sources "${CMAKE_CURRENT_BINARY_DIR}/game_packaging.c")
	endif()
	add_executable(${id} ${game_sources} "${AMIGA_FRONTEND_MAIN}"
		"${CMAKE_CURRENT_BINARY_DIR}/game_dumps.c" "${CMAKE_CURRENT_BINARY_DIR}/game_menu.c")
	target_link_libraries(${id} PRIVATE amiga_frontend)
	set_target_properties(${id} PROPERTIES LINKER_LANGUAGE CXX)
	amiga_warnings(${id})
	if(MINGW)
		target_link_options(${id} PRIVATE -static -mwindows)
	elseif(UNIX AND NOT APPLE)
		target_link_options(${id} PRIVATE -static-libgcc -static-libstdc++)
	endif()
endfunction()

# --- tests -------------------------------------------------------------------------------------

enable_testing()

if(NOT TARGET test_image)
	amiga_dumps("${AMIGA_ROOT}/tests/dumps.txt" "${CMAKE_BINARY_DIR}/tests/test_dumps.c")
	add_executable(test_image "${AMIGA_ROOT}/tests/test_image.c" "${AMIGA_ROOT}/frontend/image.c" "${CMAKE_BINARY_DIR}/tests/test_dumps.c")
	target_include_directories(test_image PRIVATE "${AMIGA_ROOT}/frontend")
	target_link_libraries(test_image PRIVATE amiga_core miniz)
	amiga_warnings(test_image)
	add_test(NAME test_image COMMAND test_image)
	add_executable(test_hack "${AMIGA_ROOT}/tests/test_hack.c")
	target_link_libraries(test_hack PRIVATE amiga_core)
	amiga_warnings(test_hack)
	add_test(NAME test_hack COMMAND test_hack)
	add_executable(test_state "${AMIGA_ROOT}/tests/test_state.c")
	target_link_libraries(test_state PRIVATE amiga_core)
	amiga_warnings(test_state)
	add_test(NAME test_state COMMAND test_state)
	add_executable(test_dos "${AMIGA_ROOT}/tests/test_dos.c")
	target_link_libraries(test_dos PRIVATE amiga_core)
	amiga_warnings(test_dos)
	add_test(NAME test_dos COMMAND test_dos)
endif()
