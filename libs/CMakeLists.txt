# Build binary, inherit setting from parent
set(liba_SRCS fru.c)

add_library(fru-static STATIC fru.c)
add_library(fru-shared SHARED fru.c)
