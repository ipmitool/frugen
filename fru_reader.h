#include "fru.h"
void safe_read(int fd, void *buffer, size_t length);
void fd_read_field(int fd, uint8_t *out);
void fd_fill_custom_fields(int fd, fru_reclist_t **reclist);

fru_chassis_area_t *read_fru_chassis_area(int fd);
fru_board_area_t *read_fru_board_area(int fd);
