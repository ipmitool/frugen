#include "fru.h"
void safe_read(int fd, void *buffer, size_t length);
fru_chassis_area_t *read_fru_chassis_area(int fd);
fru_board_area_t *read_fru_board_area(int fd);
fru_product_area_t *read_fru_product_area(int fd);
