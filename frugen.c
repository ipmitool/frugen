#ifndef VERSION
#define VERSION "v1.0-dirty-orphan"
#endif

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
#include "fru_reader.h"
#include "smbios.h"
#include "fatal.h"

#ifdef __HAS_JSON__
#include <json-c/json.h>
#endif

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
long hex2byte(const char *hex) {
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

bool datestr_to_tv(const char *datestr, struct timeval *tv)
{
	struct tm tm = {0};
	time_t time;
	char *ret;

#if __WIN32__ || __WIN64__
	/* There is no strptime() in Windows C libraries */
	int mday, mon, year, hour, min, sec;

	if(6 != sscanf(datestr, "%d/%d/%d %d:%d:%d", &mday, &mon, &year, &hour, &min, &sec)) {
		return false;
	}

	tm.tm_mday = mday;
	tm.tm_mon = mon - 1;
	tm.tm_year = year - 1900;
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;
#else
	ret = strptime(datestr, "%d/%m/%Y%t%T", &tm);
	if (!ret || *ret != 0)
		return false;
#endif
	tzset(); // Set up local timezone
	tm.tm_isdst = -1; // Use local timezone data in mktime
	time = mktime(&tm); // Here we have local time since local Epoch
	tv->tv_sec = time + timezone; // Convert to UTC
	tv->tv_usec = 0;
	return true;
}

fru_field_t * fru_encode_custom_binary_field(const char *hexstr)
{
	int len, i;
	uint8_t *buf;
	fru_field_t *rec;
	len = strlen(hexstr);
	debug(3, "The custom field is marked as binary, length is %d", len);
	if (len % 2)
		fatal("Must provide even number of nibbles for binary data");
	len /= 2;
	buf = malloc(len);
	if (!buf)
		fatal("Failed to allocate a custom buffer");
	for (i = 0; i < len; i++) {
		long byte = hex2byte(hexstr + 2 * i);
		debug(4, "[%d] %c %c => 0x%02lX",
		      i, hexstr[2 * i], hexstr[2 * i + 1], byte);
		if (byte < 0)
			fatal("Invalid hex data provided for binary custom attribute");
		buf[i] = byte;
	}
	rec = fru_encode_data(len, buf);
	free(buf);

	return rec;
}

#ifdef __HAS_JSON__
bool json_fill_fru_area_fields(json_object *jso, int count,
                               const char *fieldnames[],
                               char *fields[])
{
	int i;
	json_object *jsfield;
	bool data_in_this_area = false;
	for (i = 0; i < count; i++) {
		json_object_object_get_ex(jso, fieldnames[i], &jsfield);
		if (jsfield) {
			const char *s = json_object_get_string(jsfield);
			debug(2, "Field %s '%s' loaded from JSON",
			         fieldnames[i], s);
			fru_loadfield(fields[i], s);
			data_in_this_area = true;
		}
	}

	return data_in_this_area;
}

bool json_fill_fru_area_custom(json_object *jso, fru_reclist_t **custom)
{
	int i, alen;
	json_object *jsfield;
	array_list *array;
	bool data_in_this_area = false;
	fru_reclist_t *custptr;

	if (!custom)
		return false;

	json_object_object_get_ex(jso, "custom", &jsfield);
	if (!jsfield)
		return false;

	array = json_object_get_array(jsfield);
	if (!array)
		return false;

	alen = json_object_array_length(jsfield);
	if (!alen)
		return false;

	for (i = 0; i < alen; i++) {
		const char *type = NULL;
		const char *data = NULL;
		json_object *item, *ifield;

		item = json_object_array_get_idx(jsfield, i);
		if (!item) continue;

		json_object_object_get_ex(item, "type", &ifield);
		if (!ifield || !(type = json_object_get_string(ifield))) {
			debug(3, "Using automatic text encoding for custom field %d", i);
			type = "auto";
		}

		json_object_object_get_ex(item, "data", &ifield);
		if (!ifield || !(data = json_object_get_string(ifield))) {
			debug(3, "Emtpy data or no data at all found for custom field %d", i);
			continue;
		}

		debug(4, "Custom pointer before addition was %p", *custom);
		custptr = add_reclist(custom);
		debug(4, "Custom pointer after addition is %p", *custom);

		if (!custptr)
			return false;

		if (!strcmp(type, "binary")) {
			custptr->rec = fru_encode_custom_binary_field(data);
		} else {
			custptr->rec = fru_encode_data(LEN_AUTO, data);
		}

		if (!custptr->rec) {
			fatal("Failed to encode custom field. Memory allocation or field length problem.");
		}
		debug(2, "Custom field %i has been loaded from JSON at %p->rec = %p", i, *custom, (*custom)->rec);
		data_in_this_area = true;
	}

	debug(4, "Traversing all custom fields...");
	custptr = *custom;
	while(custptr) {
		debug(4, "Custom %p, next %p", custptr, custptr->next);
		custptr = custptr->next;
	}

	return data_in_this_area;
}
#endif /* __HAS_JSON__ */

int main(int argc, char *argv[])
{
	int i;
	int fd;
	int opt;
	int lindex;

	fru_t *fru;
	size_t size;

	bool cust_binary = false; // Flag: treat the following custom attribute as binary
	bool no_curr_date = false; // Flag: don't use current timestamp if no 'date' is specified

	const char *fname = NULL;

	fru_area_t areas[FRU_MAX_AREAS] = {
		{ .atype = FRU_INTERNAL_USE },
		{ .atype = FRU_CHASSIS_INFO },
		{ .atype = FRU_BOARD_INFO, },
		{ .atype = FRU_PRODUCT_INFO, },
		{ .atype = FRU_MULTIRECORD }
	};

	fru_exploded_chassis_t chassis = { 0, .type = SMBIOS_CHASSIS_UNKNOWN };
	fru_exploded_board_t board = { 0, .lang = LANG_ENGLISH };
	fru_exploded_product_t product = { 0, .lang = LANG_ENGLISH };

	tzset();
	gettimeofday(&board.tv, NULL);
	board.tv.tv_sec += timezone;

	struct option options[] = {
		/* Display usage help */
		{ .name = "help",          .val = 'h', .has_arg = false },

		/* Increase verbosity */
		{ .name = "verbose",       .val = 'v', .has_arg = false },

		/* Mark the following '*-custom' data as binary */
		{ .name = "binary",        .val = 'b', .has_arg = false },

		/* Set input file format to JSON */
		{ .name = "json",          .val = 'j', .has_arg = false },

		/* Set input file format to raw binary */
		{ .name = "raw",          .val = 'r', .has_arg = false },

		/* Set file to load the data from */
		{ .name = "from",          .val = 'z', .has_arg = true },

		/* Chassis info area related options */
		{ .name = "chassis-type",  .val = 't', .has_arg = true },
		{ .name = "chassis-pn",    .val = 'a', .has_arg = true },
		{ .name = "chassis-serial",.val = 'c', .has_arg = true },
		{ .name = "chassis-custom",.val = 'C', .has_arg = true },
		/* Board info area related options */
		{ .name = "board-pname",   .val = 'n', .has_arg = true },
		{ .name = "board-mfg",     .val = 'm', .has_arg = true },
		{ .name = "board-date",    .val = 'd', .has_arg = true },
		{ .name = "board-date-unspec", .val = 'u', .has_arg = false },
		{ .name = "board-pn",      .val = 'p', .has_arg = true },
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
		{ .name = "prod-atag",     .val = 'A', .has_arg = true },
		{ .name = "prod-custom",   .val = 'P', .has_arg = true },
	};

	const char *option_help[] = {
		['h'] = "Display this help",
		['v'] = "Increase program verbosity (debug) level",
		['b'] = "Mark the next --*-custom option's argument as binary.\n\t\t"
			    "Use hex string representation for the next custom argument.\n\t\t"
			    "\n\t\t"
			    "Example: frugen --binary --board-custom 0012DEADBEAF\n"
			    "\n\t\t"
			    "There must be an even number of characters in a 'binary' argument",
		['j'] = "Set input text file format to JSON (default). Specify before '--from'",
		['r'] = "Set input file format to raw binary. Specify before '--from'",
		['z'] = "Load FRU information from a text file",
		/* Chassis info area related options */
		['t'] = "Set chassis type (hex). Defaults to 0x02 ('Unknown')",
		['a'] = "Set chassis part number",
		['c'] = "Set chassis serial number",
		['C'] = "Add a custom chassis information field, may be used multiple times",
		/* Board info area related options */
		['n'] = "Set board product name",
		['m'] = "Set board manufacturer name",
		['d'] = "Set board manufacturing date/time, use \"DD/MM/YYYY HH:MM:SS\" format.\n\t\t"
		        "By default the current system date/time is used unless -u is not specified",
		['u'] = "Don't use current system date/time for board mfg. date, use 'Unspecified'",
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
		['A'] = "Set product Asset Tag",
		['P'] = "Add a custom product information field, may be used multiple times"
	};

	bool has_chassis  = false,
	     has_board    = false,
	     has_bdate    = false,
	     has_product  = false,
	     has_internal = false,
	     has_multirec = false;

	bool use_json = false; /* TODO: Add more input formats, consider libconfig */
	bool use_binary = false;

	do {
		fru_reclist_t **custom = NULL;
		lindex = -1;
		opt = getopt_long(argc, argv, "bvht:a:c:C:n:m:d:up:s:f:B:N:G:M:V:S:F:A:P:", options, &lindex);
		switch (opt) {
			case 'b': // binary
				debug(2, "Next custom field will be considered binary");
				cust_binary = true;
				break;
			case 'v': // verbose
				debug_level++;
				debug(debug_level, "Verbosity level set to %d", debug_level);
				break;
			case 'h': // help
				printf("FRU Generator %s (c) 2016-2017, Alexander Amelkin <alexander@amelkin.msk.ru>\n", VERSION);
				printf("\n"
					   "Usage: frugen [options] <filename>\n"
					   "\n"
					   "Options:\n\n");
				for (i = 0; i < ARRAY_SZ(options); i++) {
					printf("\t--%s%s\n" /* "\t-%c%s\n" */, options[i].name,
						                      options[i].has_arg ? " <argument>" : "");
					printf("\t\t%s.\n\n", option_help[options[i].val]);
				}
				printf("Example:\n"
				       "\tfrugen --board-mfg \"Biggest International Corp.\" \\\n"
				       "\t       --board-pname \"Some Cool Product\" \\\n"
				       "\t       --board-pn \"BRD-PN-123\" \\\n"
				       "\t       --board-date \"10/1/2017 12:58:00\" \\\n"
				       "\t       --board-serial \"01171234\" \\\n"
				       "\t       --board-file \"Command Line\" \\\n"
				       "\t       --binary --board-custom \"01020304FEAD1E\" \\\n"
				       "\t       fru.bin\n");
				exit(0);
				break;

			case 'j': // json
				use_json = true;
				if (use_binary) {
					fatal("Can't specify --json and --raw together");
				}
				break;

			case 'r': // binary
				use_binary = true;
				if (use_json) {
					fatal("Can't specify --json and --raw together");
				}
				break;

			case 'z': // from
				debug(2, "Will load FRU information from file %s", optarg);
				if (use_json) {
#ifdef __HAS_JSON__
					json_object *jstree, *jso, *jsfield;
					json_object_iter iter;

					debug(2, "Data format is JSON");
					/* Allocate a new object and load contents from file */
					jstree = json_object_from_file(optarg);
					if (NULL == jstree)
						fatal("Failed to load JSON FRU object from %s", optarg);

					json_object_object_foreachC(jstree, iter) {
						jso = iter.val;
						if (!strcmp(iter.key, "internal")) {
							debug(1, "Internal area is not yet supported, JSON object skipped");
							continue;
						} else if (!strcmp(iter.key, "chassis")) {
							const char *fieldname[] = { "pn", "serial" };
							char *field[] = { chassis.pn, chassis.serial };
							json_object_object_get_ex(jso, "type", &jsfield);
							if (jsfield) {
								chassis.type = json_object_get_int(jsfield);
								debug(2, "Chassis type 0x%02X loaded from JSON", chassis.type);
								has_chassis = true;
							}
							/* Now get values for the string fields */
							has_chassis |= json_fill_fru_area_fields(jso, ARRAY_SZ(field), fieldname, field);
							has_chassis |= json_fill_fru_area_custom(jso, &chassis.cust);
						} else if (!strcmp(iter.key, "board")) {
							const char *fieldname[] = { "mfg", "pname", "pn", "serial", "file" };
							char *field[] = { board.mfg, board.pname, board.pn, board.serial, board.file };
							/* Get values for non-string fields */
#if 0 /* TODO: Language support is not implemented yet */
							json_object_object_get_ex(jso, "lang", &jsfield);
							if (jsfield) {
								board.lang = json_object_get_int(jsfield);
								debug(2, "Board language 0x%02X loaded from JSON", board.lang);
								has_board = true;
							}
#endif
							json_object_object_get_ex(jso, "date", &jsfield);
							if (jsfield) {
								const char *date = json_object_get_string(jsfield);
								debug(2, "Board date '%s' will be loaded from JSON", date);
								if (!datestr_to_tv(date, &board.tv))
									fatal("Invalid board date/time format in JSON file");
								has_board = true;
								has_bdate = true;
							}
							/* Now get values for the string fields */
							has_board |= json_fill_fru_area_fields(jso, ARRAY_SZ(field), fieldname, field);
							has_board |= json_fill_fru_area_custom(jso, &board.cust);
						} else if (!strcmp(iter.key, "product")) {
							const char *fieldname[] = { "mfg", "pname", "pn", "ver", "serial", "atag", "file" };
							char *field[] = { product.mfg, product.pname, product.pn, product.ver,
							                  product.serial, product.atag, product.file };
#if 0 /* TODO: Language support is not implemented yet */
							/* Get values for non-string fields */
							json_object_object_get_ex(jso, "lang", &jsfield);
							if (jsfield) {
								product.lang = json_object_get_int(jsfield);
								debug(2, "Product language 0x%02X loaded from JSON", product.lang);
								has_product = true;
							}
#endif
							/* Now get values for the string fields */
							has_product |= json_fill_fru_area_fields(jso, ARRAY_SZ(field), fieldname, field);
							has_product |= json_fill_fru_area_custom(jso, &product.cust);
						} else if (!strcmp(iter.key, "multirecord")) {
							debug(1, "Multirecord area is not yet supported, JSON object skipped");
							continue;
						}
					}

					/* Deallocate the JSON object */
					json_object_put(jstree);
#else
					fatal("JSON support was disabled at compile time");
#endif
				}
				else if (use_binary) {
					int fd = open(optarg, O_RDONLY);
					if (fd < 0) {
						fatal("Failed to open file: %s", strerror(errno));
					}

					char common_header[8];
					safe_read(fd, common_header, 8);

					// Common Header Format Version = common_header[0]
					// Internal Use Area Starting Offset = common_header[1]
					uint16_t chassis_start_offset = 8 * common_header[2];
					uint16_t board_start_offset = 8 * common_header[3];
					uint16_t product_start_offset = 8 * common_header[4];
					// MultiRecord Area Starting Offset = 8 * common_header[5]
					// Padding = common_header[6] = 0
					// Checksum = common_header[7]

					// For the above offsets, an offset of 0 is valid and implies
					// that the field is not present.

					bool data_has_chassis = chassis_start_offset != 0;
					bool data_has_board = board_start_offset != 0;
					bool data_has_product = product_start_offset != 0;

					if (data_has_chassis) {
                        lseek(fd, chassis_start_offset, SEEK_SET);
                        fru_chassis_area_t *chassis_raw =
                            read_fru_chassis_area(fd);
                        bool success = fru_decode_chassis_info(
                            chassis_raw, &chassis);
                        if (!success)
                            fatal("Failed to decode chassis!");
                        free(chassis_raw);

						printf("Loaded chassis serial '%s'\n", chassis.serial);
						printf("Loaded chassis pn '%s'\n", chassis.pn);

						has_chassis = true;
					}
					if (data_has_board) {
						lseek(fd, board_start_offset, SEEK_SET);

						fru_board_area_t *board_raw = read_fru_board_area(fd);
						bool success = fru_decode_board_info(board_raw, &board);

						has_board = true;
						has_bdate = true;
					}
					if (data_has_product) {
						lseek(fd, product_start_offset, SEEK_SET);

						uint8_t product_header[3];
						safe_read(fd, product_header, 3);
						if (product_header[0] != 1)
							fatal("Unsupported Board Info Area Format Version");
						// Product Info Area Length = 8 * product_header[1]
						product.lang = product_header[2];

						fd_read_field(fd, product.mfg);
						fd_read_field(fd, product.pname);
						fd_read_field(fd, product.pn);
						fd_read_field(fd, product.ver);
						fd_read_field(fd, product.serial);
						fd_read_field(fd, product.atag);
						fd_read_field(fd, product.file);
						fd_fill_custom_fields(fd, &product.cust);

						has_product = true;
					}

					close(fd);
				}
				else {
					fatal("The requested input file format is not supported");
				}
				break;

			case 't': // chassis-type
				chassis.type = strtol(optarg, NULL, 16);
				debug(2, "Chassis type will be set to 0x%02X from [%s]", chassis.type, optarg);
				has_chassis = true;
				break;
			case 'a': // chassis-pn
				fru_loadfield(chassis.pn, optarg);
				has_chassis = true;
				break;
			case 'c': // chassis-serial
				fru_loadfield(chassis.serial, optarg);
				has_chassis = true;
				break;
			case 'C': // chassis-custom
				debug(2, "Custom chassis field [%s]", optarg);
				has_chassis = true;
				custom = &chassis.cust;
				break;
			case 'n': // board-pname
				fru_loadfield(board.pname, optarg);
				has_board = true;
				break;
			case 'm': // board-mfg
				fru_loadfield(board.mfg, optarg);
				has_board = true;
				break;
			case 'd': // board-date
				debug(2, "Board manufacturing date will be set from [%s]", optarg);
				if (!datestr_to_tv(optarg, &board.tv))
					fatal("Invalid date/time format, use \"DD/MM/YYYY HH:MM:SS\"");
				has_board = true;
				break;
			case 'u': // board-date-unspec
				no_curr_date = true;
				break;
			case 'p': // board-pn
				fru_loadfield(board.pn, optarg);
				has_board = true;
				break;
			case 's': // board-sn
				fru_loadfield(board.serial, optarg);
				has_board = true;
				break;
			case 'f': // board-file
				fru_loadfield(board.file, optarg);
				has_board = true;
				break;
			case 'B': // board-custom
				debug(2, "Custom board field [%s]", optarg);
				has_board = true;
				custom = &board.cust;
				break;
			case 'N': // prod-name
				fru_loadfield(product.pname, optarg);
				has_product = true;
				break;
			case 'G': // prod-mfg
				fru_loadfield(product.mfg, optarg);
				has_product = true;
				break;
			case 'M': // prod-modelpn
				fru_loadfield(product.pn, optarg);
				has_product = true;
				break;
			case 'V': // prod-version
				fru_loadfield(product.ver, optarg);
				has_product = true;
				break;
			case 'S': // prod-serial
				fru_loadfield(product.serial, optarg);
				has_product = true;
				break;
			case 'F': // prod-file
				fru_loadfield(product.file, optarg);
				has_product = true;
				break;
			case 'A': // prod-atag
				fru_loadfield(product.atag, optarg);
				has_product = true;
				break;
			case 'P': // prod-custom
				debug(2, "Custom product field [%s]", optarg);
				has_product = true;
				custom = &product.cust;
				break;
			case '?':
				exit(1);
			default:
				break;
		}

		if (custom) {
			fru_reclist_t *custptr;
			debug(3, "Adding a custom field from argument [%s]", optarg);
			custptr = add_reclist(custom);

			if (!custptr)
				fatal("Failed to allocate a custom record list entry");

			if (cust_binary) {
				custptr->rec = fru_encode_custom_binary_field(optarg);
			}
			else {
				debug(3, "The custom field will be auto-typed");
				custptr->rec = fru_encode_data(LEN_AUTO, optarg);
			}
			if (!custptr->rec) {
				fatal("Failed to encode custom field. Memory allocation or field length problem.");
			}
			cust_binary = false;
		}
	} while (opt != -1);

	if (optind >= argc)
		fatal("Filename must be specified");

	fname = argv[optind];
	debug(1, "FRU info data will be stored in %s", fname);

	if (has_internal) {
		fatal("Internal use area is not yet supported in the library");
	}

	if (has_chassis) {
		int e;
		fru_chassis_area_t *ci = NULL;
		debug(1, "FRU file will have a chassis information area");
		debug(3, "Chassis information area's custom field list is %p", chassis.cust);
		ci = fru_encode_chassis_info(&chassis);
		e = errno;
		free_reclist(chassis.cust);

		if (ci)
			areas[FRU_CHASSIS_INFO].data = ci;
		else {
			errno = e;
			fatal("Error allocating a chassis info area: %m");
		}
	}

	if (has_board) {
		int e;
		fru_board_area_t *bi = NULL;
		debug(1, "FRU file will have a board information area");
		debug(3, "Board information area's custom field list is %p", board.cust);
		debug(3, "Board date is specified? = %d", has_bdate);
		debug(3, "Board date use unspec? = %d", no_curr_date);
		if (!has_bdate && no_curr_date) {
			debug(1, "Using 'unspecified' board mfg. date");
			board.tv = (struct timeval){0};
		}

		bi = fru_encode_board_info(&board);
		e = errno;
		free_reclist(board.cust);

		if (bi)
			areas[FRU_BOARD_INFO].data = bi;
		else {
			errno = e;
			fatal("Error allocating a board info area: %m");
		}
	}

	if (has_product) {
		int e;
		fru_product_area_t *pi = NULL;
		debug(1, "FRU file will have a product information area");
		debug(3, "Product information area's custom field list is %p", product.cust);
		pi = fru_encode_product_info(&product);

		e = errno;
		free_reclist(product.cust);

		if (pi)
			areas[FRU_PRODUCT_INFO].data = pi;
		else {
			errno = e;
			fatal("Error allocating a product info area: %m");
		}
	}

	if (has_multirec) {
		fatal("Multirecord area is not yet supported in the library");
	}

	fru = fru_create(areas, &size);
	if (!fru) {
		fatal("Error allocating a FRU file buffer: %m");
	}

	debug(1, "Writing %lu bytes of FRU data", (long unsigned int)FRU_BYTES(size));

	fd = open(fname,
#if __WIN32__ || __WIN64__
	          O_CREAT | O_TRUNC | O_WRONLY | O_BINARY,
#else
	          O_CREAT | O_TRUNC | O_WRONLY,
#endif
	          0644);

	if (fd < 0)
		fatal("Couldn't create file %s: %m", fname);

	if (0 > write(fd, fru, FRU_BYTES(size)))
		fatal("Couldn't write to %s: %m", fname);

	free(fru);
}
