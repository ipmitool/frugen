cmake_minimum_required(VERSION 3.0)
if(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

execute_process(COMMAND
	git
	describe
	--always
	--long
	--dirty
	WORKING_DIRECTORY
	"${CMAKE_CURRENT_SOURCE_DIR}"
	RESULT_VARIABLE
	res
	OUTPUT_VARIABLE
	gitver
	ERROR_QUIET
	OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT res EQUAL 0)
	set(gitver "UNKNOWN")
endif()

string(REPLACE "-" "." gitver "${gitver}")

add_definitions(-DVERSION="${gitver}")

add_executable(frugen frugen.c)
add_library(fru_reader STATIC fru_reader.c)
add_library(fru-static STATIC fru.c)
add_library(fru-shared SHARED fru.c)
find_library(JSON_LIB json-c)
SET_TARGET_PROPERTIES(fru-static PROPERTIES OUTPUT_NAME fru CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(fru-shared PROPERTIES OUTPUT_NAME fru CLEAN_DIRECT_OUTPUT 1)
target_link_libraries(frugen fru-static fru_reader)
if (JSON_LIB)
	message (STATUS "Using JSON Library found at " ${JSON_LIB})
	add_definitions(-D__HAS_JSON__)
	target_link_libraries(frugen ${JSON_LIB})
else (JSON_LIB)
	message (WARNING "JSON library *NOT* found. JSON support *disabled*!")
endif (JSON_LIB)

# To make frugen 32-bit, uncomment the following lines or use an external toolchain file
# set_target_properties(fru_reader-static PROPERTIES OUTPUT_NAME fru CLEAN_DIRECT_OUTPUT 1)
#set_target_properties(frugen PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
#set_target_properties(fru-static PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
#set_target_properties(fru-shared PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")

# The following are for debugging only
#target_compile_options(fru-static PUBLIC -g3 -O0)
#target_compile_options(frugen PUBLIC -g3 -O0)
#target_compile_definitions(frugen PUBLIC __STANDALONE__)
