#############################################
# cmake requirements
#############################################
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
	OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT res EQUAL 0)
	set(gitver "UNKNOWN")
endif()

#############################################
# Set version
#############################################
string(REPLACE "-" "." gitver "${gitver}")

add_definitions(-DVERSION="${gitver}")

#############################################
# Compile options
#############################################
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_FLAGS_DEBUG "-g -O0")

# Test 32/64 bits, only tested on Ubuntu 16.04
if( NOT ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8"))
    message(STATUS "Target is 32 bits")
    set(CMAKE_C_FLAGS "-m32")
endif( NOT ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8"))

#############################################
# Build options
#############################################
# Set directories
set(SRC_DIR src)
set(LIB_DIR libs)
set(INC_DIR include)
include_directories(${INC_DIR})

# Dive into libs directory
add_subdirectory(${LIB_DIR})
add_subdirectory(${SRC_DIR})
