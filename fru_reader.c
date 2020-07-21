#include <stdio.h>
#include <unistd.h>
#include "fru.h"
#include "fru_reader.h"
#include "fatal.h"

void safe_read(int fd, void *buffer, size_t length) {
    size_t total_bytes_read = 0;
    while (total_bytes_read != length) {
        ssize_t bytes_read = read(
			fd, buffer + total_bytes_read, length - total_bytes_read);
        if (bytes_read == -1)
            fatal("Error reading file");

        total_bytes_read += bytes_read;
    }
}

void fd_read_field(int fd, uint8_t *out) {
	uint8_t typelen;
	safe_read(fd, &typelen, 1);

	size_t length = FRU_FIELDDATALEN(typelen);
    uint8_t raw_data[FRU_FIELDMAXARRAY];
    safe_read(fd, raw_data, length);

	if(!fru_decode_data(typelen, raw_data, out, FRU_FIELDMAXARRAY))
		fatal("Could not decode field");
}

void fd_fill_custom_fields(int fd, fru_reclist_t **reclist) {
	while (true) {
		uint8_t typelen;
		safe_read(fd, &typelen, 1);
		if (typelen == 0xc1)
			break;

		fru_reclist_t *custom_field = add_reclist(reclist);
		if (custom_field == NULL)
			fatal("Error allocating custom field");

		size_t length = FRU_FIELDDATALEN(typelen);
		uint8_t *data = malloc(length + 1);
		if (data == NULL)
			fatal("Error allocating custom field");
		safe_read(fd, data, length);
		data[length] = 0;

		custom_field->rec = fru_encode_data(LEN_AUTO, data);
		free(data);
	}
}


/**
 * Allocate and read a fru_chassis_area_t from a file descriptor
 */
fru_chassis_area_t *read_fru_chassis_area(int fd) {
    size_t base_len = sizeof(fru_chassis_area_t);
    fru_chassis_area_t *area = malloc(base_len);
    safe_read(fd, area, base_len);
    size_t data_len = 8 * area->blocks;
    area = realloc(area, base_len + data_len);
    safe_read(fd, &area->data, data_len);

    return area;
}

/**
 * Allocate and read a fru_board_area_t from a file descriptor
 */
fru_board_area_t *read_fru_board_area(int fd) {
    size_t base_len = sizeof(fru_board_area_t);
    fru_board_area_t *area = malloc(base_len);
    safe_read(fd, area, base_len);
    size_t data_len = 8 * area->blocks;
    area = realloc(area, base_len + data_len);
    safe_read(fd, &area->data, data_len);

    return area;
}
