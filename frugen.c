#define _GNU_SOURCE
#include <getopt.h>
#include <stdbool.h>

#include <stdio.h>
#include "fru.h"

int main(int argc, char *argv[])
{
	char opt;
	fru_reclist_t chassis[] = {
		{ NULL, chassis[1] }, // Chassis type
		{ NULL, chassis[2] }, // Chassis part number
		{ NULL, NULL },       // Chassis serial number
	};

	uint8_t board_lang = 0;
	struct timeval board_tv;
	gettimeofday(&board_tv, NULL);
	fru_reclist_t board[] = {
		{ NULL, board[1] }, // Board manufacturer name
		{ NULL, board[2] }, // Board product name
		{ NULL, board[3] }, // Board serial number
		{ NULL, board[4] }, // Board part number
		{ NULL, board[5] }, // Board FRU file ID
		{ NULL, NULL },

	};
	fru_reclist_t product[] = {
	};
	
	struct option options[] = {
		/* Chassis info area related options */
		{ .name = "chassis-type",  .val = 't', .has_arg = true },
		{ .name = "chassis-pn",    .val = 'a', .has_arg = true },
		{ .name = "chassis-sn",    .val = 'c', .has_arg = true },
		/* Board info area related options */
		{ .name = "board-name",    .val = 'n', .has_arg = true },
		{ .name = "board-mfg",     .val = 'm', .has_arg = true },
		{ .name = "board-date",    .val = 'd', .has_arg = true },
		{ .name = "board-part",    .val = 'p', .has_arg = true },
		{ .name = "board-serial",  .val = 's', .has_arg = true },
		{ .name = "board-product", .val = 'r', .has_arg = true },
		{ .name = "board-file",    .val = 'f', .has_arg = true },
		/* Product info area related options */
		{ .name = "prod-name",     .val = 'N', .has_arg = true },
		{ .name = "prod-mfg",      .val = 'M', .has_arg = true },
		{ .name = "prod-modelpn",  .val = 'P', .has_arg = true },
		{ .name = "prod-version",  .val = 'V', .has_arg = true },
		{ .name = "prod-serial",   .val = 'S', .has_arg = true },
		{ .name = "prod-file",     .val = 'F', .has_arg = true },
	}
	do {
		opt = getopt_long(argc, argv, "t:a:c:n:m:d:p:s:r:f:N:M:P:V:S:F:", options, NULL);
		switch (opt) {
			case 't': // chassis-type
				break;
			case 'a': // chassis-pn
				break;
			case 'c': // chassis-sn
				break;
			case 'n': // board-name
				break;
			case 'm': // board-mfg
				break;
			case 'd': // board-date
				break;
			case 'p': // board-part
				break;
			case 'r': // board-product
				break;
			case 'r': // board-serial
				break;
			case 'f': // board-file
				break;
			case '?':
				fprintf(stderr, "Unknown option: %c\n", opt);
				exit(1);
			default;
				break;
		}
	} while (opt != -1);
}
