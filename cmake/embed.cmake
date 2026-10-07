if(CMAKE_SCRIPT_MODE_FILE)
	string(REPLACE "|" ";" _files "${FILES}")
	set(_arrays "")
	set(_table "")
	set(_i 0)
	foreach(_file IN LISTS _files)
		get_filename_component(_name "${_file}" NAME)
		file(READ "${_file}" _hex HEX)
		string(LENGTH "${_hex}" _hex_length)
		math(EXPR _size "${_hex_length} / 2")
		if(_size EQUAL 0)
			set(_bytes "0")
		else()
			string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
		endif()
		string(APPEND _arrays "static const uint8_t asset_${_i}[] = {${_bytes}};\n")
		string(APPEND _table "\t{\"${_name}\", asset_${_i}, ${_size}},\n")
		math(EXPR _i "${_i} + 1")
	endforeach()
	file(WRITE "${OUT}"
		"#include \"menu.h\"\n\n${_arrays}\n"
		"const struct SdlAsset ${SYMBOL}[] = {\n${_table}\t{0, 0, 0}\n};\n")
else()
	function(amiga_embed out symbol)
		add_custom_command(
			OUTPUT "${out}"
			COMMAND "${CMAKE_COMMAND}" -DOUT=${out} -DSYMBOL=${symbol} "-DFILES=$<JOIN:${ARGN},|>" -P "${AMIGA_ROOT}/cmake/embed.cmake"
			DEPENDS ${ARGN} "${AMIGA_ROOT}/cmake/embed.cmake"
			VERBATIM)
	endfunction()
endif()
