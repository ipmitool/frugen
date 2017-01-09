#define _GNU_SOURCE
#include <getopt.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "fru.h"

int main(int argc, char *argv[])
{
	int fd;
	int opt;
	int lindex;

	fru_t *fru;
	size_t size;

	fru_area_t areas[FRU_MAX_AREAS] = {
		{ .atype = FRU_INTERNAL_USE },
		{ .atype = FRU_CHASSIS_INFO },
		{ .atype = FRU_BOARD_INFO, },
		{ .atype = FRU_PRODUCT_INFO, },
		{ .atype = FRU_MULTIRECORD }
	};

	uint8_t board_lang = LANG_ENGLISH, prod_lang = LANG_ENGLISH;
	struct timeval board_tv;
	gettimeofday(&board_tv, NULL);

	const char *chassis[] = {
		[FRU_CHASSIS_TYPE]   = NULL,
		[FRU_CHASSIS_PARTNO] = NULL,
		[FRU_CHASSIS_SERIAL] = NULL,
	};
	fru_reclist_t chassis_cust = { NULL, NULL };

	const char *board[] = {
		[FRU_BOARD_MFG]      = NULL,
		[FRU_BOARD_PRODNAME] = NULL,
		[FRU_BOARD_SERIAL]   = NULL,
		[FRU_BOARD_PARTNO]   = NULL,
		[FRU_BOARD_FILE]     = NULL,
	};
	fru_reclist_t board_cust = { NULL, NULL };

	const char *product[] = {
		[FRU_PROD_MFG]     = NULL,
		[FRU_PROD_NAME]    = NULL,
		[FRU_PROD_MODELPN] = NULL,
		[FRU_PROD_VERSION] = NULL,
		[FRU_PROD_SERIAL]  = NULL,
		[FRU_PROD_ASSET]   = NULL,
		[FRU_PROD_FILE]    = NULL,
	};
	fru_reclist_t prod_cust = { NULL, NULL };

	struct option options[] = {
		/* Chassis info area related options */
		{ .name = "chassis-type",   .val = 't', .has_arg = true },
		{ .name = "chassis-pn",     .val = 'a', .has_arg = true },
		{ .name = "chassis-sn",     .val = 'c', .has_arg = true },
		{ .name = "chassis-custom", .val = 'C', .has_arg = true },
		/* Board info area related options */
		{ .name = "board-prodname",.val = 'n', .has_arg = true },
		{ .name = "board-mfg",     .val = 'm', .has_arg = true },
		{ .name = "board-date",    .val = 'd', .has_arg = true },
		{ .name = "board-part",    .val = 'p', .has_arg = true },
		{ .name = "board-serial",  .val = 's', .has_arg = true },
		{ .name = "board-file",    .val = 'f', .has_arg = true },
		{ .name = "board-custom",  .val = 'B', .has_arg = true },
		/* Product info area related options */
		{ .name = "prod-name",     .val = 'N', .has_arg = true },
		{ .name = "prod-mfg",      .val = 'M', .has_arg = true },
		{ .name = "prod-modelpn",  .val = 'O', .has_arg = true },
		{ .name = "prod-version",  .val = 'V', .has_arg = true },
		{ .name = "prod-serial",   .val = 'S', .has_arg = true },
		{ .name = "prod-file",     .val = 'F', .has_arg = true },
		{ .name = "prod-custom",   .val = 'P', .has_arg = true },
	};

	bool has_chassis  = false,
	     has_board    = false,
	     has_product  = false,
	     has_internal = false,
	     has_multirec = false;

	do {
		bool option_supported = false; // TODO: Remove this when all options are supported
		lindex = -1;
		opt = getopt_long(argc, argv, "t:a:c:C:n:m:d:p:s:f:B:N:M:O:V:S:F:P:", options, &lindex);
		switch (opt) {
			case 't': // chassis-type
				has_chassis = true;
				break;
			case 'a': // chassis-pn
				has_chassis = true;
				break;
			case 'c': // chassis-sn
				has_chassis = true;
				break;
			case 'C': // chassis-custom
				has_chassis = true;
				break;
			case 'n': // board-name
				board[FRU_BOARD_PRODNAME] = optarg;
				has_board = true;
				option_supported = true;
				break;
			case 'm': // board-mfg
				board[FRU_BOARD_MFG] = optarg;
				has_board = true;
				option_supported = true;
				break;
			case 'd': // board-date
				has_board = true;
				break;
			case 'p': // board-part
				board[FRU_BOARD_PARTNO] = optarg;
				has_board = true;
				option_supported = true;
				break;
			case 's': // board-serial
				board[FRU_BOARD_SERIAL] = optarg;
				has_board = true;
				option_supported = true;
				break;
			case 'f': // board-file
				board[FRU_BOARD_FILE] = optarg;
				has_board = true;
				option_supported = true;
				break;
			case 'B': // board-custom
				has_board = true;
				break;
			case 'N': // prod-name
				has_product = true;
				break;
			case 'M': // prod-mfg
				has_product = true;
				break;
			case 'O': // prod-modelpn
				has_product = true;
				break;
			case 'V': // prod-version
				has_product = true;
				break;
			case 'S': // prod-serial
				has_product = true;
				break;
			case 'F': // prod-file
				has_product = true;
				break;
			case 'P': // prod-custom
				has_product = true;
				break;
			case '?':
				exit(1);
			default:
				break;
		}
		if (opt != -1 && !option_supported) {
			if(lindex >= 0) {
				fprintf(stderr, "Option '--%s' is valid but is not yet supported\n", options[lindex].name);
			}
			else {
				fprintf(stderr, "Option '-%c' is valid but is not yet supported\n", opt);
			}
			exit(1);
		}
	} while (opt != -1);


	if (has_board) {
		fru_board_area_t *bi = NULL;
		bi = fru_board_info(board_lang,
							&board_tv,
							board[FRU_BOARD_MFG],
							board[FRU_BOARD_PRODNAME],
							board[FRU_BOARD_SERIAL],
							board[FRU_BOARD_PARTNO],
							board[FRU_BOARD_FILE],
							&board_cust);
		if (bi)
			areas[FRU_BOARD_INFO].data = bi;
		else {
			perror("Error allocating a board info area");
			exit(1);
		}
	}

	if (has_product) {
		fru_product_area_t *pi = NULL;
		pi = fru_product_info(prod_lang,
							  product[FRU_PROD_MFG],
							  product[FRU_PROD_NAME],
							  product[FRU_PROD_MODELPN],
							  product[FRU_PROD_VERSION],
							  product[FRU_PROD_SERIAL],
							  product[FRU_PROD_ASSET],
							  product[FRU_PROD_FILE],
							  &prod_cust);
		if (pi)
			areas[FRU_PRODUCT_INFO].data = pi;
		else {
			perror("Error allocating a product info area");
			exit(1);
		}
	}

	fru = fru_create(areas, &size);
	if (!fru) {
		perror("Error allocating a FRU file buffer");
		exit(1);
	}

	printf("Writing %lu bytes of FRU data.\n", FRU_BYTES(size));
	
	fd = open("frux.bin", O_CREAT | O_TRUNC | O_WRONLY, 0644);

	if (fd < 0)
		perror("open");

	if (0 > write(fd, fru, FRU_BYTES(size)))
		perror("write2");

	free(fru);
}
