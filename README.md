# frugen / libfru

## Introduction

This project was incepted to eventually create a universal, full-featured IPMI
FRU Information generator / editor library and command line tool, written in
full compliance with IPMI FRU Information Storage Definition v1.0, rev. 1.3., see
http://www.intel.com/content/www/us/en/servers/ipmi/ipmi-platform-mgt-fru-infostorage-def-v1-0-rev-1-3-spec-update.html

## libfru

So far supported in libfru:

  * Data encoding into all the defined formats (binary, BCD plus, 6-bit ASCII, language code specific text).
    The exceptions are:

    * all text is always encoded as if the language code was English (ASCII, 1 byte per character)
    * encoding is selected automatically based on value range of the supplied data, only binary format
      can be enforced by specifying the length other than LEN_AUTO.

  * Data decoding from all the declared formats.
    Exception: Unicode is not supported

  * Chassis information area creation
  * Board information area creation
  * Product information area creation
  * FRU file creation (in a memory buffer)

NOT supported:

  * Internal use area creation/reservation in a fru file buffer
  * Multirecord area creation

## frugen

The frugen tool supports the following (limitations imposed by the libfru library):

  * Board area creation (including custom fields)
  * Product area creation (including custom fields)
  * Chassis area creation (including custom fields)

The limitations:

  * All data fields (except custom) are always treated as ASCII text, and the encoding
    is automatically selected based on the byte range of the provided data. Custom fields
    may be forced to be binary using --binary option.
  * Internal use and Multirecord areas are not supported

For the most up-to-date information on the frugen tool invocation and options, please
use `frugen -h`.

### JSON

The frugen tool supports JSON files. You may specify all the FRU info fields (mind the
general tool limitations) in a file and use it as an input for the tool:

    frugen --json --from=example.json fru.bin

An example file 'example.json' is provided for your reference.

## Building
### json support

* Offcial site:
    * https://github.com/json-c/json-c/wiki

* Install libjson-c in Ubuntu 16.04
    sudo apt install libjson-c-dev

### Linux

    cmake .
    make

### Windows (cross-compiled on Linux)

You will need a MingW32 toolchain. This chapter is written in assumption you're
using x86\_64-w64-mingw32.

First of all you will need to create a x86\_64-w64-mingw32-toolchain.cmake
file describing your cross-compilation toolchain.

This file assumes that you use $HOME/mingw-install as an installation prefix
for all mingw32-compiled libraries (e.g., libjson-c).

    # the name of the target operating system
    SET(CMAKE_SYSTEM_NAME Windows)

    SET(MINGW32_INSTALL_DIR $ENV{HOME}/mingw-install)

    # which compilers to use for C and C++
    SET(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    SET(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
    SET(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

    # here is the target environment located
    SET(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32/ ${MINGW32_INSTALL_DIR})

    # adjust the default behaviour of the FIND_XXX() commands:
    # search headers and libraries in the target environment, search 
    # programs in the host environment
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

    # let the compiler find the includes and linker find the libraries
    # MinGW linker doesn't accept absolute library paths found
    # by find_library()
    include_directories(BEFORE ${MINGW32_INSTALL_DIR}/include)
    set(CMAKE_LIBRARY_PATH ${MINGW32_INSTALL_DIR}/lib ${MINGW32_INSTALL_DIR}/usr/lib ${CMAKE_LIBRARY_PATH})

Once you have that file, build the tool as follows:

    cmake -DCMAKE_TOOLCHAIN_FILE=x86\_64-w64-mingw32-toolchain.cmake  .
    make

## Contact information

Should you have any questions or proposals, please write to alexander [at] amelkin [dot] msk [dot] ru.
