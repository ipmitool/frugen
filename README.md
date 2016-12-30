# frugen

This *will be* a universal, full-featured IPMI FRU Information generator / editor
written in full compliance with IPMI FRU Information Storage Definition v1.0, rev. 1.1.,
see http://www.intel.com/content/www/us/en/servers/ipmi/information-storage-definition.html

So far supported (as library functions):

  * Data encoding into all the declared formats (binary, BCD plus, 6-bit ASCII, language code specific text).
    The exceptions are:

    * all text is always encoded as if the language code was English (ASCII, 1 byte per character)
    * encoding is selected automatically based on value range of the supplied data

  * Data decoding from all the declared formats.
    Exception: Unicode is not supported

  * Board information area creation
  * Product information area creation
  * FRU file creation (in a memory buffer)

