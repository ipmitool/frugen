#include "fru.h"
fru_chassis_area_t *read_fru_chassis_area(int fd);
void safe_read(int fd, void *buffer, size_t length);
void fd_read_field(int fd, uint8_t *out);
void fd_fill_custom_fields(int fd, fru_reclist_t **reclist);
