add_executable(frugen frugen.c)
# Checking json lib
find_library(JSON_LIB json-c)
if (JSON_LIB)
	message (STATUS "Using JSON Library found at " ${JSON_LIB})
	add_definitions(-D__HAS_JSON__)
	target_link_libraries(frugen ${JSON_LIB})
else (JSON_LIB)
	message (WARNING "JSON library json-c *NOT* found. JSON support *disabled*!")
endif (JSON_LIB)

#

target_link_libraries(frugen fru-static)

SET_TARGET_PROPERTIES(fru-static PROPERTIES OUTPUT_NAME fru CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(fru-shared PROPERTIES OUTPUT_NAME fru CLEAN_DIRECT_OUTPUT 1)
