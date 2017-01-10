#define VERSION "1.0 beta"

#define _GNU_SOURCE
#include <getopt.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "fru.h"
#include "smbios.h"

#define fatal(fmt, args...) do {  \
	fprintf(stderr, fmt, ##args); \
	fprintf(stderr, "\n");        \
	exit(1);                      \
} while(0)

volatile int debug_level = 0;
#define debug(level, fmt, args...) do { \
	int e = errno;                      \
	if(level <= debug_level) {          \
		printf("DEBUG: ");              \
		errno = e;                      \
		printf(fmt, ##args);            \
		printf("\n");                   \
		errno = e;                      \
	}                                   \
} while(0)


/**
 * Convert 2 bytes of hex string into a binary byte
 */
long hex2byte(char *hex) {
	static const long hextable[256] = {
		[0 ... 255] = -1,
		['0'] = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
		['A'] = 10, 11, 12, 13, 14, 15,
		['a'] = 10, 11, 12, 13, 14, 15
	};

	if (!hex) return -1;

	long hi = hextable[hex[0]];
	long lo = hextable[hex[1]];

	debug(9, "hi = %02lX, lo = %02lX", hi, lo);

	if (hi < 0 || lo < 0)
		return -1;

	return ((hi << 4) | lo);
}

int main(int argc, char *argv[])
{
	int i;
	int fd;
	int opt;
	int lindex;

	fru_t *fru;
	size_t size;

	bool cust_binary = false; // Flag: treat the following custom attribute as binary

	const char *fname = NULL;

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

	uint8_t chassis_type = SMBIOS_UNKNOWN;
	const char *chassis[] = {
		[FRU_CHASSIS_PARTNO] = NULL,
		[FRU_CHASSIS_SERIAL] = NULL,
	};
	fru_reclist_t *chassis_cust = NULL;

	const char *board[] = {
		[FRU_BOARD_MFG]      = NULL,
		[FRU_BOARD_PRODNAME] = NULL,
		[FRU_BOARD_SERIAL]   = NULL,
		[FRU_BOARD_PARTNO]   = NULL,
		[FRU_BOARD_FILE]     = NULL,
	};
	fru_reclist_t *board_cust = NULL;

	const char *product[] = {
		[FRU_PROD_MFG]     = NULL,
		[FRU_PROD_NAME]    = NULL,
		[FRU_PROD_MODELPN] = NULL,
		[FRU_PROD_VERSION] = NULL,
		[FRU_PROD_SERIAL]  = NULL,
		[FRU_PROD_ASSET]   = NULL,
		[FRU_PROD_FILE]    = NULL,
	};
	fru_reclist_t *prod_cust = NULL;

	struct option options[] = {
		/* Mark the following '*-custom' data as binary */
		{ .name = "binary",  .val = 'b', .has_arg = false },
		{ .name = "verbose", .val = 'v', .has_arg = false },
		{ .name = "help",    .val = 'h', .has_arg = false },
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
		{ .name = "prod-mfg",      .val = 'G', .has_arg = true },
		{ .name = "prod-modelpn",  .val = 'M', .has_arg = true },
		{ .name = "prod-version",  .val = 'V', .has_arg = true },
		{ .name = "prod-serial",   .val = 'S', .has_arg = true },
		{ .name = "prod-file",     .val = 'F', .has_arg = true },
		{ .name = "prod-custom",   .val = 'P', .has_arg = true },
	};

	const char *option_help[] = {
		['b'] = "Mark the next --*-custom option's argument as binary.\n"
			    "Use hex string representation for the next custom argument.\n"
			    "\n"
			    "Example: frugen --binary --board-custom 0012DEADBEAF\n"
			    "\n"
			    "There must be an even number of characters in a 'binary' argument",
		['v'] = "Increase program verbosity (debug) level",
		['h'] = "Display this help",
		/* Chassis info area related options */
		['t'] = "Set chassis type (hex). Defaults to 0x02 ('Unknown')",
		['a'] = "Set chassis part number",
		['c'] = "Set chassis serial number",
		['C'] = "Add a custom chassis information field, may be used mupltiple times",
		/* Board info area related options */
		['n'] = "Set board product name",
		['m'] = "Set board manufacturer name",
		['d'] = "Set board manufacturing date/time, use \"DD/MM/YYYY HH:MM:SS\" format.\n"
		        "By default the current system date/time is used",
		['p'] = "Set board part number",
		['s'] = "Set board serial number",
		['f'] = "Set board FRU file ID",
		['B'] = "Add a custom board information field, may be used multiple times",
		/* Product info area related options */
		['N'] = "Set product name",
		['G'] = "Set product manufacturer name",
		['M'] = "Set product model / part number",
		['V'] = "Set product version",
		['S'] = "Set product serial number",
		['F'] = "Set product FRU file ID",
		['P'] = "Add a custom product information field, may be used multiple times"
	};

	bool has_chassis  = false,
	     has_board    = false,
	     has_product  = false,
	     has_internal = false,
	     has_multirec = false;

	do {
		bool option_supported = false; // TODO: Remove this when all options are supported
		fru_reclist_t **custom = NULL;
		lindex = -1;
		opt = getopt_long(argc, argv, "bvht:a:c:C:n:m:d:p:s:f:B:N:G:M:V:S:F:P:", options, &lindex);
		switch (opt) {
			case 'b': // binary
				debug(2, "Next custom field will be considered binary");
				cust_binary = true;
				option_supported = true;
				break;
			case 'v': // verbose
				debug_level++;
				debug(debug_level, "Verbosity level set to %d", debug_level);
				option_supported = true;
				break;
			case 'h': // help
				printf("FRU Generator v%s (c) 2016, Alexander Amelkin <alexander@amelkin.msk.ru>\n", VERSION);
				printf("\n"
					   "Usage: frugen [options] <filename>\n"
					   "\n"
					   "Options:\n\n");
				for (i = 0; i < ARRAY_SZ(options); i++) {
					printf("--%s%s\n-%c%s\n", options[i].name,
						                      options[i].has_arg ? " <argument>" : "",
						                      options[i].val,
						                      options[i].has_arg ? " <argument>" : "");
					printf("%s.\n\n", option_help[options[i].val]);
				}
				exit(0);
				break;

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
				debug(2, "Custom chassis field [%s]", optarg);
				has_chassis = true;
				custom = &chassis_cust;
				option_supported = true;
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
			case 'd': { // board-date
					struct tm tm;
					time_t time;
					char *ret;
					debug(2, "Board manufacturing date will be set from [%s]", optarg);
					ret = strptime(optarg, "%d/%m/%Y%t%T", &tm);
					if (!ret || *ret != 0)
						fatal("Invalid date/time format, use \"DD/MM/YYYY HH:MM:SS\"");
					tzset(); // Set up local timezone
					tm.tm_isdst = -1; // Use local timezone data in mktime
					time = mktime(&tm); // Here we have local time since local Epoch
					board_tv.tv_sec = time + timezone; // Convert to UTC
					board_tv.tv_usec = 0;
					has_board = true;
					option_supported = true;
				}
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
				debug(2, "Custom board field [%s]", optarg);
				has_board = true;
				custom = &board_cust;
				option_supported = true;
				break;
			case 'N': // prod-name
				has_product = true;
				break;
			case 'G': // prod-mfg
				has_product = true;
				break;
			case 'M': // prod-modelpn
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
				debug(2, "Custom product field [%s]", optarg);
				has_product = true;
				custom = &prod_cust;
				option_supported = true;
				break;
			case '?':
				exit(1);
			default:
				break;
		}
		if (opt != -1 && !option_supported) {
			if(lindex >= 0)
				fatal("Option '--%s' is valid but is not yet supported", options[lindex].name);
			else
				fatal("Option '-%c' is valid but is not yet supported", opt);
		}

		if (custom) {
			debug(3, "Adding a custom field from argument [%s]", optarg);
			*custom = add_reclist(*custom);

			if (!*custom)
				fatal("Failed to allocate a custom record list entry");

			if (cust_binary) {
				int len, i;
				uint8_t *buf;
				len = strlen(optarg);
				debug(3, "The custom field is marked as binary, length is %d", len);
				if (len % 2)
					fatal("Must provide even number of nibbles for binary data");
				len /= 2;
				buf = malloc(len);
				if (!buf)
					fatal("Failed to allocate a custom buffer");
				for (i = 0; i < len; i++) {
					long byte = hex2byte(optarg + 2 * i);
					debug(4, "[%d] %c %c => 0x%02lX",
						     i, optarg[2 * i], optarg[2 * i + 1], byte);
					if (byte < 0)
						fatal("Invalid hex data provided for binary custom attribute");
					buf[i] = byte;
				}
				(*custom)->rec = fru_encode_data(len, buf);
				free(buf);
				if (!(*custom)->rec)
					fatal("Failed to allocate a custom field");
			}
			else {
				debug(3, "The custom field will be auto-typed");
				(*custom)->rec = fru_encode_data(LEN_AUTO, optarg);
			}
			cust_binary = false;
		}
	} while (opt != -1);

	if (optind >= argc)
		fatal("Filename must be specified");

	fname = argv[optind];
	debug(1, "FRU info data will be stored in %s", fname);

	if (has_board) {
		fru_board_area_t *bi = NULL;
		debug(1, "FRU file will have a board information area");
		debug(3, "Board information area's custom field list is %p", board_cust);
		bi = fru_board_info(board_lang,
							&board_tv,
							board[FRU_BOARD_MFG],
							board[FRU_BOARD_PRODNAME],
							board[FRU_BOARD_SERIAL],
							board[FRU_BOARD_PARTNO],
							board[FRU_BOARD_FILE],
							board_cust);

		free_reclist(board_cust);

		if (bi)
			areas[FRU_BOARD_INFO].data = bi;
		else {
			perror("Error allocating a board info area");
			exit(1);
		}
	}

	if (has_product) {
		fru_product_area_t *pi = NULL;
		debug(1, "FRU file will have a product information area");
		debug(3, "Product information area's custom field list is %p", prod_cust);
		pi = fru_product_info(prod_lang,
							  product[FRU_PROD_MFG],
							  product[FRU_PROD_NAME],
							  product[FRU_PROD_MODELPN],
							  product[FRU_PROD_VERSION],
							  product[FRU_PROD_SERIAL],
							  product[FRU_PROD_ASSET],
							  product[FRU_PROD_FILE],
							  prod_cust);

		free_reclist(prod_cust);

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

	debug(1, "Writing %lu bytes of FRU data", FRU_BYTES(size));

	fd = open(fname, O_CREAT | O_TRUNC | O_WRONLY, 0644);

	if (fd < 0)
		fatal("Couldn't create file %s: %m", fname);

	if (0 > write(fd, fru, FRU_BYTES(size)))
		fatal("Couldn't write to %s: %m", fname);

	free(fru);
}
