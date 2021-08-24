# frugen / libfru

## License

This work is dual-licensed under Apache 2.0 and GPL 2.0 (or any later version)
for the frugen utility, or under Apache 2.0 and Lesser GPL 2.0 (or any later version)
for the fru library.

You can choose between one of them if you use this work.

`SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later`

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
  * Multirecord area creation with the following record types:

    * Management Access Record with the following subtypes:

      * System UUID

  * FRU file creation (in a memory buffer)

NOT supported:

  * Internal use area creation/reservation in a fru file buffer
  * Miltirecord area record types other than listed above

## frugen

The frugen tool supports the following (limitations imposed by the libfru library):

  * Board area creation (including custom fields)
  * Product area creation (including custom fields)
  * Chassis area creation (including custom fields)
  * Multirecord area creation (see libfru supported types above)

The limitations:

  * All data fields (except custom) are always treated as ASCII text, and the encoding
    is automatically selected based on the byte range of the provided data. Custom fields
    may be forced to be binary using --binary option.
  * Internal use area is not supported

For the most up-to-date information on the frugen tool invocation and options, please
use `frugen -h`, below is an example of the output of that command:

```
FRU Generator v1.3.2.g1429f89 (c) 2016-2021, Alexander Amelkin <alexander@amelkin.msk.ru>

Usage: frugen [options] <filename>

Options:

	-h, --help
		Display this help.

	-v, --verbose
		Increase program verbosity (debug) level.

	-b, --binary
		Mark the next --*-custom option's argument as binary.
		Use hex string representation for the next custom argument.

		Example: frugen --binary --board-custom 0012DEADBEAF

		There must be an even number of characters in a 'binary' argument.

	-I, --ascii
		Disable auto-encoding on all fields, force ASCII.
		Out of ASCII range data will still result in binary encoding.

	-j, --json
		Set input text file format to JSON (default). Specify before '--from'.

	-z, --from <argument>
		Load FRU information from a text file.

	-t, --chassis-type <argument>
		Set chassis type (hex). Defaults to 0x02 ('Unknown').

	-a, --chassis-pn <argument>
		Set chassis part number.

	-c, --chassis-serial <argument>
		Set chassis serial number.

	-C, --chassis-custom <argument>
		Add a custom chassis information field, may be used multiple times.

	-n, --board-pname <argument>
		Set board product name.

	-m, --board-mfg <argument>
		Set board manufacturer name.

	-d, --board-date <argument>
		Set board manufacturing date/time, use "DD/MM/YYYY HH:MM:SS" format.
		By default the current system date/time is used unless -u is not specified.

	-u, --board-date-unspec
		Don't use current system date/time for board mfg. date, use 'Unspecified'.

	-p, --board-pn <argument>
		Set board part number.

	-s, --board-serial <argument>
		Set board serial number.

	-f, --board-file <argument>
		Set board FRU file ID.

	-B, --board-custom <argument>
		Add a custom board information field, may be used multiple times.

	-N, --prod-name <argument>
		Set product name.

	-G, --prod-mfg <argument>
		Set product manufacturer name.

	-M, --prod-modelpn <argument>
		Set product model / part number.

	-V, --prod-version <argument>
		Set product version.

	-S, --prod-serial <argument>
		Set product serial number.

	-F, --prod-file <argument>
		Set product FRU file ID.

	-A, --prod-atag <argument>
		Set product Asset Tag.

	-P, --prod-custom <argument>
		Add a custom product information field, may be used multiple times.

	-U, --mr-uuid <argument>
		Set System Unique ID (UUID/GUID).

Example:
	frugen --board-mfg "Biggest International Corp." \
	       --board-pname "Some Cool Product" \
	       --board-pn "BRD-PN-123" \
	       --board-date "10/1/2017 12:58:00" \
	       --board-serial "01171234" \
	       --board-file "Command Line" \
	       --binary --board-custom "01020304FEAD1E" \
	       fru.bin
```

### JSON

__Dependency:__ json-c library (https://github.com/json-c/json-c)

The frugen tool supports JSON files. You may specify all the FRU info fields (mind the
general tool limitations) in a file and use it as an input for the tool:

    frugen --json --from=example.json fru.bin

An example file 'example.json' is provided for your reference.

## Building

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

Should you have any questions or proposals, please feel free to:

 * Ask a question via https://github.com/ipmitool/frugen/discussions
 * Submit changes as a pull request via https://github.com/ipmitool/frugen/pulls
 * Report a problem by creating an issue at https://github.com/ipmitool/frugen/issues
