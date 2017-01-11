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

## Contact information

Should you have any questions or proposals, please write to alexander [at] amelkin [dot] msk [dot] ru.
