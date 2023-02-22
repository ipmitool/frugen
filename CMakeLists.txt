cmake_minimum_required(VERSION 3.0)
project(frugen C)
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

option(BINARY_32BIT "compile 32bit version" OFF)
option(DEBUG_OUTPUT "show extra debug output" OFF)
option(DEBUG_SYMBOLS "compile with debug symbols" OFF)
option(DEBUG_NO_OPTIMIZE "do not optimize the code" OFF)
option(COMPILER_WARNINGS "show compiler warnings" ON)

if (DEBUG_OUTPUT)
  add_definitions(-DDEBUG)
endif(DEBUG_OUTPUT)

add_definitions(-DVERSION="${gitver}")

function(set_compiler_flags TARGETS)
  foreach(TARGET IN LISTS TARGETS)
    if ( CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
      target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wunreachable-code)
    endif()
  endforeach()
endfunction()

function(set_compiler_options TARGETS OPTS)
  foreach(TARGET IN LISTS TARGETS)
    target_compile_options(${TARGET} PRIVATE ${OPTS})
  endforeach()
endfunction()

add_executable(frugen frugen.c)
add_executable(frugen-static frugen.c)
add_library(fru-static STATIC fru.c)
add_library(fru-shared SHARED fru.c)
find_library(JSON_LIB json-c)
SET_TARGET_PROPERTIES(fru-static PROPERTIES OUTPUT_NAME fru CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(fru-shared PROPERTIES OUTPUT_NAME fru CLEAN_DIRECT_OUTPUT 1)
target_link_libraries(frugen fru-shared)
target_link_libraries(frugen-static fru-static -static)
if (JSON_LIB)
	message (STATUS "Using JSON Library found at " ${JSON_LIB})
	add_definitions(-D__HAS_JSON__)
	target_link_libraries(frugen ${JSON_LIB})
	target_link_libraries(frugen-static ${JSON_LIB} -static)
else (JSON_LIB)
	message (WARNING "JSON library *NOT* found. JSON support *disabled*!")
endif (JSON_LIB)

if (COMPILER_WARNINGS)
  set_compiler_flags("frugen;frugen-static;fru-static;fru-shared")
endif(COMPILER_WARNINGS)

# To make frugen 32-bit, uncomment the following lines or use an external toolchain file
if (BINARY_32BIT)
  set_target_properties(frugen PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
  set_target_properties(frugen-static PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
  set_target_properties(fru-static PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
  set_target_properties(fru-shared PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
endif (BINARY_32BIT)

# The following are for debugging only
if (DEBUG_SYMBOLS)
  set_compiler_options("frugen;frugen-static;fru-static;fru-shared" "-g3")
endif (DEBUG_SYMBOLS)
if (DEBUG_NO_OPTIMIZE)
  set_compiler_options("frugen;frugen-static;fru-static;fru-shared" "-O0")
endif (DEBUG_NO_OPTIMIZE)
# target_compile_definitions(frugen PUBLIC __STANDALONE__)
