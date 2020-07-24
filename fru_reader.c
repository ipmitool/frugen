#include <stdio.h>
#include <unistd.h>
#include "fru.h"
#include "fru_reader.h"
#include "fatal.h"

static void safe_read(int fd, uint8_t *buffer, size_t length) {
	if (!buffer)
		fatal("Cannot read into NULL buffer");

	size_t total_bytes_read = 0;
	while (total_bytes_read != length) {
		ssize_t bytes_read = read(
			fd, buffer + total_bytes_read, length - total_bytes_read);
		if (bytes_read == -1)
			fatal("Error reading file");
		if (bytes_read == 0)
			fatal("Reached end of file");

		total_bytes_read += bytes_read;
	}
}

fru_t *read_fru_header(int fd) {
	fru_t *fru = malloc(sizeof(fru_t));
	if (!fru)
		return NULL;
	safe_read(fd, (uint8_t*)fru, sizeof(fru_t));
	return fru;
}

/**
 * Allocate and read a fru_chassis_area_t from a file descriptor
 */
fru_chassis_area_t *read_fru_chassis_area(int fd) {
	size_t base_len = sizeof(fru_chassis_area_t);
	fru_chassis_area_t *area = malloc(base_len);
	if (!area)
		return NULL;
	safe_read(fd, (uint8_t*)area, base_len);
	size_t data_len = 8 * area->blocks;
	area = realloc(area, data_len);
	if (!area)
		return NULL;
	safe_read(fd, (uint8_t*)&area->data, data_len - base_len);

	return area;
}

/**
 * Allocate and read a fru_board_area_t from a file descriptor
 */
fru_board_area_t *read_fru_board_area(int fd) {
	size_t base_len = sizeof(fru_board_area_t);
	fru_board_area_t *area = malloc(base_len);
	if (!area)
		return NULL;
	safe_read(fd, (uint8_t*)area, base_len);
	size_t data_len = 8 * area->blocks;
	area = realloc(area, data_len);
	if (!area)
		return NULL;
	safe_read(fd, (uint8_t*)&area->data, data_len - base_len);

	return area;
}

/**
 * Allocate and read a fru_product_area_t from a file descriptor
 */
fru_product_area_t *read_fru_product_area(int fd) {
	size_t base_len = sizeof(fru_product_area_t);
	fru_product_area_t *area = malloc(base_len);
	if (!area)
		return NULL;
	safe_read(fd, (uint8_t*)area, base_len);
	size_t data_len = 8 * area->blocks;
	area = realloc(area, data_len);
	if (!area)
		return NULL;
	safe_read(fd, (uint8_t*)&area->data, data_len - base_len);

	return area;
}
