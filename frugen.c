/** @file
 *  @brief FRU generator utility
 *
 *  Copyright (C) 2016-2021 Alexander Amelkin <alexander@amelkin.msk.ru>
 *  SPDX-License-Identifier: GPL-2.0-or-later OR Apache-2.0
 */
#ifndef VERSION
#define VERSION "v1.2-dirty-orphan"
#endif

#define COPYRIGHT_YEARS "2016-2021"
#define MAX_FILE_SIZE 1L * 1024L * 1024L

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
#include <limits.h>
#include "fru.h"
#include "smbios.h"

#ifdef __HAS_JSON__
#include <json-c/json.h>
#endif

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

static
int typelen2ind(uint8_t field) {
	if (FIELD_TYPE_T(field) < TOTAL_FIELD_TYPES)
		return FIELD_TYPE_T(field);
	else
		return FIELD_TYPE_AUTO;
}

static
void hexdump(const void *data, size_t len)
{
	size_t i;
	const unsigned char *buf = data;

	for (i = 0; i < len; ++i) {
		if (0 == (i % 16))
			printf("DEBUG: %04x: ", (unsigned int)i);

		printf("%02X ", buf[i]);

		if (15 == (i % 16))
			printf("\n");
	}

	if (i % 16) {
		printf("\n");
	}
}

#define debug_dump(level, data, len, fmt, args...) do { \
	debug(level, fmt, ##args); \
	if (level <= debug_level) hexdump(data, len); \
} while(0)

/**
 * Convert 2 bytes of hex string into a binary byte
 */
static
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

static
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

static
uint8_t * fru_encode_binary_string(size_t *len, const char *hexstr)
{
	int i;
	uint8_t *buf;

	if (!len) {
		fatal("BUG: No storage for hex string length provided");
	}

	*len = strlen(hexstr);
	debug(3, "The field is marked as binary, length is %zi", *len);
	if (*len % 2)
		fatal("Must provide even number of nibbles for binary data");
	*len /= 2;
	buf = malloc(*len);
	if (!buf)
		fatal("Failed to allocate a buffer for binary data");
	for (i = 0; i < *len; i++) {
		long byte = hex2byte(hexstr + 2 * i);
		debug(4, "[%d] %c %c => 0x%02lX",
		      i, hexstr[2 * i], hexstr[2 * i + 1], byte);
		if (byte < 0)
			fatal("Invalid hex data provided for binary attribute");
		buf[i] = byte;
	}
	return buf;
}

static
fru_field_t * fru_encode_custom_binary_field(const char *hexstr)
{
	size_t len;
	uint8_t *buf = fru_encode_binary_string(&len, hexstr);
	fru_field_t *rec;
	rec = fru_encode_data(len, buf);
	free(buf);

	return rec;
}

#ifdef __HAS_JSON__

#if (JSON_C_MAJOR_VERSION == 0 && JSON_C_MINOR_VERSION < 13)
int
json_object_to_fd(int fd, struct json_object *obj, int flags)
{
	// implementation is copied from json-c v0.13
	int ret;
	const char *json_str;
	unsigned int wpos, wsize;

	if (!(json_str = json_object_to_json_string_ext(obj,flags))) {
		return -1;
	}

	wsize = (unsigned int)(strlen(json_str) & UINT_MAX); /* CAW: probably unnecessary, but the most 64bit safe */
	wpos = 0;
	while(wpos < wsize) {
		if((ret = write(fd, json_str + wpos, wsize-wpos)) < 0) {
			return -1;
		}

		/* because of the above check for ret < 0, we can safely cast and add */
		wpos += (unsigned int)ret;
	}

	return 0;
}
#endif

static
bool json_fill_fru_area_fields(json_object *jso, int count,
                               const char *fieldnames[],
                               typed_field_t *fields[])
{
	int i;
	json_object *jsfield;
	bool data_in_this_area = false;
	for (i = 0; i < count; i++) {
		if (json_object_object_get_ex(jso, fieldnames[i], &jsfield)) {
			// field is an object
			json_object *typefield, *valfield;
			if (json_object_object_get_ex(jsfield, "type", &typefield) &&
			    json_object_object_get_ex(jsfield, "data", &valfield)) {
				// expected subfields are type and val
				const char *type = json_object_get_string(typefield);
				const char *val = json_object_get_string(valfield);
				if (!strcmp("binary", type)) {
					fatal("Binary format not yet implemented");
				} else if (!strcmp("bcdplus", type)) {
					fields[i]->type = FIELD_TYPE_BCDPLUS;
				} else if (!strcmp("6bitascii", type)) {
					fields[i]->type = FIELD_TYPE_SIXBITASCII;
				} else if (!strcmp("text", type)) {
					fields[i]->type = FIELD_TYPE_TEXT;
				} else {
					debug(1, "Unknown type %s for field '%s'",
					      type, fieldnames[i]);
					continue;
				}
				fru_loadfield(fields[i]->val, val);
				debug(2, "Field %s '%s' (%s) loaded from JSON",
				      fieldnames[i], val, type);
				data_in_this_area = true;
			} else {
				const char *s = json_object_get_string(jsfield);
				debug(2, "Field %s '%s' loaded from JSON",
				      fieldnames[i], s);
				fru_loadfield(fields[i]->val, s);
				fields[i]->type = FIELD_TYPE_AUTO;
				data_in_this_area = true;
			}
		}
	}

	return data_in_this_area;
}

static
bool json_fill_fru_area_custom(json_object *jso, fru_reclist_t **custom)
{
	int i, alen;
	json_object *jsfield;
	bool data_in_this_area = false;
	fru_reclist_t *custptr;

	if (!custom)
		return false;

	json_object_object_get_ex(jso, "custom", &jsfield);
	if (!jsfield)
		return false;

	if (json_object_get_type(jsfield) != json_type_array)
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

		if (!strcmp("binary", type)) {
			custptr->rec = fru_encode_custom_binary_field(data);
		} else if (!strcmp("bcdplus", type)) {
			custptr->rec = fru_encode_data(LEN_BCDPLUS, data);
		} else if (!strcmp("6bitascii", type)) {
			custptr->rec = fru_encode_data(LEN_6BITASCII, data);
		} else if (!strcmp("text", type)) {
			custptr->rec = fru_encode_data(LEN_TEXT, data);
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

static
bool json_fill_fru_mr_reclist(json_object *jso, fru_mr_reclist_t **mr_reclist)
{
	int i, alen;
	fru_mr_reclist_t *mr_reclist_tail;
	bool has_multirec = false;

	if (!mr_reclist)
		goto out;

	if (json_object_get_type(jso) != json_type_array)
		goto out;

	alen = json_object_array_length(jso);
	if (!alen)
		goto out;

	debug(4, "Multirecord area record list is initially at %p", *mr_reclist);
	mr_reclist_tail = add_mr_reclist(mr_reclist);
	if (!mr_reclist_tail)
		fatal("JSON: Failed to allocate multirecord area list");

	debug(4, "Multirecord area record list is now at %p", *mr_reclist);
	debug(4, "Multirecord area record list tail is at %p", mr_reclist_tail);

	for (i = 0; i < alen; i++) {
		const char *type = NULL;
		json_object *item, *ifield;

		item = json_object_array_get_idx(jso, i);
		if (!item) continue;

		debug(3, "Parsing record #%d/%d", i + 1, alen);

		json_object_object_get_ex(item, "type", &ifield);
		if (!ifield || !(type = json_object_get_string(ifield))) {
			fatal("Each multirecord area record must have a type specifier");
		}

		debug(3, "Record is of type '%s'", type);

		if (!strcmp(type, "management")) {
			const char *subtype = NULL;
			json_object_object_get_ex(item, "subtype", &ifield);
			if (!ifield || !(subtype = json_object_get_string(ifield))) {
				fatal("Each management record must have a subtype");
			}

			debug(3, "Management record subtype is '%s'", subtype);

			if (!strcmp(subtype, "uuid")) {
				const unsigned char *uuid = NULL;
				json_object_object_get_ex(item, "uuid", &ifield);
				if (!ifield || !(uuid = json_object_get_string(ifield))) {
					fatal("A uuid management record must have a uuid field");
				}

				debug(3, "Parsing UUID %s", uuid);
				errno = fru_mr_uuid2rec(&mr_reclist_tail->rec, uuid);
				if (errno)
					fatal("Failed to convert UUID: %m");
				debug(2, "System UUID loaded from JSON: %s", uuid);
				has_multirec = true;
			}
			else {
				fatal("Management Access Record type '%s' "
				      "is not supported", subtype);
			}
		}
		else {
			fatal("Multirecord type '%s' is not supported", type);
			continue;
		}
	}

out:
	return has_multirec;
}

static
int json_object_add_with_type(struct json_object* obj,
                              const char* key,
                              const unsigned char* val,
                              int type) {
	struct json_object *string, *type_string, *entry;
	if ((string = json_object_new_string(val)) == NULL)
		goto STRING_ERR;

	if (type == FIELD_TYPE_AUTO) {
		entry = string;
	} else {
		if ((type_string = json_object_new_string(enc_names[type])) == NULL)
			goto TYPE_STRING_ERR;
		if ((entry = json_object_new_object()) == NULL)
			goto ENTRY_ERR;
		json_object_object_add(entry, "type", type_string);
		json_object_object_add(entry, "data", string);
	}
	if (key == NULL) {
		return json_object_array_add(obj, entry);
	} else {
		json_object_object_add(obj, key, entry);
		return 0;
	}

ENTRY_ERR:
	json_object_put(type_string);
TYPE_STRING_ERR:
	json_object_put(string);
STRING_ERR:
	return -1;
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

		/* Disable autodetection, force ASCII encoding on standard fields,
		 * Detection of binary (out of ASCII range) stays in place.
		 */
		{ .name = "ascii",         .val = 'I', .has_arg = false },

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
		/* MultiRecord area related options */
		{ .name = "mr-uuid",       .val = 'U', .has_arg = true },
	};

	const char *option_help[] = {
		['h'] = "Display this help",
		['v'] = "Increase program verbosity (debug) level",
		['b'] = "Mark the next --*-custom option's argument as binary.\n\t\t"
			    "Use hex string representation for the next custom argument.\n"
			    "\n\t\t"
			    "Example: frugen --binary --board-custom 0012DEADBEAF\n"
			    "\n\t\t"
			    "There must be an even number of characters in a 'binary' argument",
		['I'] = "Disable auto-encoding on all fields, force ASCII.\n\t\t"
			    "Out of ASCII range data will still result in binary encoding",
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
		        "By default the current system date/time is used unless -u is specified",
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
		['P'] = "Add a custom product information field, may be used multiple times",
		/* MultiRecord area related options */
		['U'] = "Set System Unique ID (UUID/GUID)",
	};

	bool has_chassis  = false,
	     has_board    = false,
	     has_bdate    = false,
	     has_product  = false,
	     has_internal = false,
	     has_multirec = false;
	fru_mr_reclist_t *mr_reclist = NULL;

	bool use_json = false; /* TODO: Add more input formats, consider libconfig */
	bool use_binary = false;

	unsigned char optstring[ARRAY_SZ(options) * 2 + 1] = {0};

	for (i = 0; i < ARRAY_SZ(options); ++i) {
		static int k = 0;
		optstring[k++] = options[i].val;
		if (options[i].has_arg)
			optstring[k++] = ':';
	}

	do {
		fru_reclist_t **custom = NULL;
		bool is_mr_record = false; // The current option is an MR area record
		lindex = -1;
		opt = getopt_long(argc, argv, optstring, options, &lindex);
		switch (opt) {
			case 'b': // binary
				debug(2, "Next custom field will be considered binary");
				cust_binary = true;
				break;
			case 'I': // ASCII
				fru_set_autodetect(false);
				break;
			case 'v': // verbose
				debug_level++;
				debug(debug_level, "Verbosity level set to %d", debug_level);
				break;
			case 'h': // help
				printf("FRU Generator %s (c) %s, "
					   "Alexander Amelkin <alexander@amelkin.msk.ru>\n",
					   VERSION, COPYRIGHT_YEARS);
				printf("\n"
					   "Usage: frugen [options] <filename>\n"
					   "\n"
					   "Options:\n\n");
				for (i = 0; i < ARRAY_SZ(options); i++) {
					printf("\t-%c, --%s%s\n" /* "\t-%c%s\n" */,
					       options[i].val,
					       options[i].name,
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
							fru_internal_use_area_t *internal;
							const char *data = json_object_get_string(jso);
							if (!data) {
								debug(2, "Internal use are w/o data, skipping");
								continue;
							}
							fru_field_t *field;
							size_t datalen;
							char *encoded_data =
								fru_encode_binary_string(&datalen, data);
							size_t blocklen = FRU_BLOCKS(datalen + sizeof(*internal));
							internal = calloc(1, FRU_BYTES(blocklen));
							if (!internal) {
								fatal("Failed to allocate memory for internal use area");
							}
							internal->ver = FRU_VER_1;
							memcpy(internal->data, encoded_data, datalen);
							free(encoded_data);
							areas[FRU_INTERNAL_USE].blocks = blocklen;
							areas[FRU_INTERNAL_USE].data = internal;

							debug(2, "Internal use area data loaded from JSON");

							has_internal = true;
							continue;
						} else if (!strcmp(iter.key, "chassis")) {
							const char *fieldname[] = { "pn", "serial" };
							typed_field_t* field[] = { &chassis.pn, &chassis.serial };
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
							typed_field_t *field[] = { &board.mfg, &board.pname, &board.pn, &board.serial, &board.file };
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
							typed_field_t *field[] = { &product.mfg, &product.pname, &product.pn, &product.ver,
							                           &product.serial, &product.atag, &product.file };
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
							debug(2, "Processing multirecord area records");
							has_multirec |= json_fill_fru_mr_reclist(jso, &mr_reclist);
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

					struct stat statbuf = {0};
					if (fstat(fd, &statbuf)) {
						fatal("Failed to get file properties: %s", strerror(errno));
					}
					if (statbuf.st_size > MAX_FILE_SIZE) {
						fatal("File too large");
					}

					uint8_t *buffer = calloc(1, statbuf.st_size);
					if (buffer == NULL) {
						fatal("Cannot allocate buffer");
					}

					if (read(fd, buffer, statbuf.st_size) != statbuf.st_size) {
						fatal("Cannot read file");
					}
					close(fd);

					fru_chassis_area_t *chassis_area =
						find_fru_chassis_area(buffer, statbuf.st_size);
					if (chassis_area) {
						if (!fru_decode_chassis_info(chassis_area, &chassis))
							fatal("Failed to decode chassis");
						has_chassis = true;
					}
					fru_board_area_t *board_area =
						find_fru_board_area(buffer, statbuf.st_size);
					if (board_area) {
						if (!fru_decode_board_info(board_area, &board))
							fatal("Failed to decode board");
						has_board = true;
					}

					fru_product_area_t *product_area =
						find_fru_product_area(buffer, statbuf.st_size);
					if (product_area) {
						if (!fru_decode_product_info(product_area, &product))
							fatal("Failed to decode product");
						has_product = true;
					}

					free(buffer);
				}
				else {
					fatal("Please specify the input file format");
				}
				break;

			case 't': // chassis-type
				chassis.type = strtol(optarg, NULL, 16);
				debug(2, "Chassis type will be set to 0x%02X from [%s]", chassis.type, optarg);
				has_chassis = true;
				break;
			case 'a': // chassis-pn
				fru_loadfield(chassis.pn.val, optarg);
				has_chassis = true;
				break;
			case 'c': // chassis-serial
				fru_loadfield(chassis.serial.val, optarg);
				has_chassis = true;
				break;
			case 'C': // chassis-custom
				debug(2, "Custom chassis field [%s]", optarg);
				has_chassis = true;
				custom = &chassis.cust;
				break;
			case 'n': // board-pname
				fru_loadfield(board.pname.val, optarg);
				has_board = true;
				break;
			case 'm': // board-mfg
				fru_loadfield(board.mfg.val, optarg);
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
				fru_loadfield(board.pn.val, optarg);
				has_board = true;
				break;
			case 's': // board-sn
				fru_loadfield(board.serial.val, optarg);
				has_board = true;
				break;
			case 'f': // board-file
				fru_loadfield(board.file.val, optarg);
				has_board = true;
				break;
			case 'B': // board-custom
				debug(2, "Custom board field [%s]", optarg);
				has_board = true;
				custom = &board.cust;
				break;
			case 'N': // prod-name
				fru_loadfield(product.pname.val, optarg);
				has_product = true;
				break;
			case 'G': // prod-mfg
				fru_loadfield(product.mfg.val, optarg);
				has_product = true;
				break;
			case 'M': // prod-modelpn
				fru_loadfield(product.pn.val, optarg);
				has_product = true;
				break;
			case 'V': // prod-version
				fru_loadfield(product.ver.val, optarg);
				has_product = true;
				break;
			case 'S': // prod-serial
				fru_loadfield(product.serial.val, optarg);
				has_product = true;
				break;
			case 'F': // prod-file
				fru_loadfield(product.file.val, optarg);
				has_product = true;
				break;
			case 'A': // prod-atag
				fru_loadfield(product.atag.val, optarg);
				has_product = true;
				break;
			case 'P': // prod-custom
				debug(2, "Custom product field [%s]", optarg);
				has_product = true;
				custom = &product.cust;
				break;
			case 'U': // All multi-record options must be listed here
			          // and processed later in a separate switch
				is_mr_record = true;
				break;

			case '?':
				exit(1);
			default:
				break;
		}

		if (is_mr_record) {
			fru_mr_reclist_t *mr_reclist_tail = add_mr_reclist(&mr_reclist);
			if (!mr_reclist_tail) {
				fatal("Failed to allocate multirecord area list");
			}
			has_multirec = true;

			switch(opt) {
				case 'U': // UUID
					errno = fru_mr_uuid2rec(&mr_reclist_tail->rec, optarg);
					if (errno) {
						fatal("Failed to convert UUID: %m");
					}
					break;
				default:
					fatal("Unknown multirecord option: %c", opt);
			}
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

	if (use_binary) {
		char timebuf[20] = {0};
		struct tm* bdtime = gmtime(&board.tv.tv_sec);
		strftime(timebuf, 20, "%d/%m/%Y %H:%M:%S", bdtime);
#ifdef __HAS_JSON__
		struct json_object *json_root = json_object_new_object();
		struct json_object *section = NULL, *temp_obj = NULL;

		if (has_chassis) {
			section = json_object_new_object();
			temp_obj = json_object_new_int(chassis.type);
			json_object_object_add(section, "type", temp_obj);
			json_object_add_with_type(section, "pn", chassis.pn.val, chassis.pn.type);
			json_object_add_with_type(section, "serial", chassis.serial.val, chassis.serial.type);
			temp_obj = json_object_new_array();
			fru_reclist_t *next = chassis.cust;
			while (next != NULL) {
				json_object_add_with_type(temp_obj, NULL, next->rec->data,
				                          typelen2ind(next->rec->typelen));
				next = next->next;
			}
			json_object_object_add(section, "custom", temp_obj);
			json_object_object_add(json_root, "chassis", section);
		}

		if (has_product) {
			section = json_object_new_object();
			temp_obj = json_object_new_int(product.lang);
			json_object_object_add(section, "lang", temp_obj);
			json_object_add_with_type(section, "mfg", product.mfg.val, product.mfg.type);
			json_object_add_with_type(section, "pname", product.pname.val, product.pname.type);
			json_object_add_with_type(section, "serial", product.serial.val, product.serial.type);
			json_object_add_with_type(section, "pn", product.pn.val, product.pn.type);
			json_object_add_with_type(section, "ver", product.ver.val, product.ver.type);
			json_object_add_with_type(section, "atag", product.atag.val, product.atag.type);
			json_object_add_with_type(section, "file", product.file.val, product.file.type);
			temp_obj = json_object_new_array();
			fru_reclist_t *next = product.cust;
			while (next != NULL) {
				json_object_add_with_type(temp_obj, NULL, next->rec->data,
				                          typelen2ind(next->rec->typelen));
				next = next->next;
			}
			json_object_object_add(section, "custom", temp_obj);
			json_object_object_add(json_root, "product", section);
		}

		if (has_board) {
			section = json_object_new_object();
			temp_obj = json_object_new_int(board.lang);
			json_object_object_add(section, "lang", temp_obj);
			json_object_add_with_type(section, "time", (unsigned char*) timebuf, 0);
			json_object_add_with_type(section, "mfg", board.mfg.val, board.mfg.type);
			json_object_add_with_type(section, "pname", board.pname.val, board.pname.type);
			json_object_add_with_type(section, "serial", board.serial.val, board.serial.type);
			json_object_add_with_type(section, "pn", board.pn.val, board.pn.type);
			json_object_add_with_type(section, "file", board.file.val, board.file.type);
			temp_obj = json_object_new_array();
			fru_reclist_t *next = board.cust;
			while (next != NULL) {
				json_object_add_with_type(temp_obj, NULL, next->rec->data,
				                          typelen2ind(next->rec->typelen));
				next = next->next;
			}
			json_object_object_add(section, "custom", temp_obj);
			json_object_object_add(json_root, "board", section);
		}
		if (has_multirec) {
			// TODO: add multirec dump
		}
		// dump to stdout
		json_object_to_fd(fileno(stdout), json_root, 0);
		json_object_put(json_root);
#else
		if (has_chassis) {
			puts("Chassis");
			printf("\ttype: %u\n", chassis.type);
			printf("\tpn(%s): %s\n", enc_names[chassis.pn.type], chassis.pn.val);
			printf("\tserial(%s): %s\n", enc_names[chassis.serial.type], chassis.serial.val);
			fru_reclist_t *next = chassis.cust;
			while (next != NULL) {
				printf("\tcustom(%s): %s\n",
				       enc_names[typelen2ind(next->rec->typelen)],
				       next->rec->data);
				next = next->next;
			}
		}

		if (has_product) {
			puts("Product");
			printf("\tlang: %u\n", product.lang);
			printf("\tmfg(%s): %s\n", enc_names[product.mfg.type], product.mfg.val);
			printf("\tpname(%s): %s\n", enc_names[product.pname.type], product.pname.val);
			printf("\tserial(%s): %s\n", enc_names[product.serial.type], product.serial.val);
			printf("\tpn(%s): %s\n", enc_names[product.pn.type], product.pn.val);
			printf("\tver(%s): %s\n", enc_names[product.ver.type], product.ver.val);
			printf("\tatag(%s): %s\n", enc_names[product.atag.type], product.atag.val);
			printf("\tfile(%s): %s\n", enc_names[product.file.type], product.file.val);
			fru_reclist_t *next = product.cust;
			while (next != NULL) {
				printf("\tcustom(%s): %s\n",
				       enc_names[typelen2ind(next->rec->typelen)],
				       next->rec->data);
				next = next->next;
			}
		}

		if (has_board) {
			puts("Board");
			printf("\tlang: %u\n", board.lang);
			printf("\ttime: %s\n", timebuf);
			printf("\tmfg(%s): %s\n", enc_names[board.mfg.type], board.mfg.val);
			printf("\tpname(%s): %s\n", enc_names[board.pname.type], board.pname.val);
			printf("\tserial(%s): %s\n", enc_names[board.serial.type], board.serial.val);
			printf("\tpn(%s): %s\n", enc_names[board.pn.type], board.pn.val);
			printf("\tfile(%s): %s\n", enc_names[board.file.type], board.file.val);
			fru_reclist_t *next = board.cust;
			while (next != NULL) {
				printf("\tcustom(%s): %s\n",
				       enc_names[typelen2ind(next->rec->typelen)],
				       next->rec->data);
				next = next->next;
			}
		}

		if (has_multirec) {
			// TODO: add multirec dump
		}
#endif
	} else {
		if (optind >= argc)
			fatal("Filename must be specified");

		fname = argv[optind];
		debug(1, "FRU info data will be stored in %s", fname);

		if (has_internal) {
			debug(1, "FRU file will have an internal use area");
			/* Nothing to do here, added for uniformity, the actual
			 * internal use area can only be initialized from file */
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
			int e;
			fru_mr_area_t *mr = NULL;
			size_t totalbytes = 0;
			debug(1, "FRU file will have a multirecord area");
			debug(3, "Multirecord area record list is %p", mr_reclist);
			mr = fru_mr_area(mr_reclist, &totalbytes);

			e = errno;
			free_reclist(mr_reclist);

			if (mr) {
				areas[FRU_MULTIRECORD].data = mr;
				areas[FRU_MULTIRECORD].blocks = FRU_BLOCKS(totalbytes);

				debug_dump(3, mr, totalbytes, "Multirecord data:");
			}
			else {
				errno = e;
				fatal("Error allocating a multirecord area: %m");
			}
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
		close(fd);
	}
}
