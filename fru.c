/** @file
 *  @brief FRU information helper functions
 */

#define DEBUG

#include "fru.h"
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifdef __STANDALONE__
#include <stdio.h>
#endif

#ifdef DEBUG
#undef DEBUG
#include <stdio.h>
#define DEBUG(f, args...) do { printf("%s:%d: ", __func__, __LINE__); printf(f,##args); } while(0)
#else
#define DEBUG(f, args...)
#endif

/*
 * Strip trailing spaces
 */
static inline void cut_tail(char *s)
{
	int i;
	for(i = strlen(s) - 1; i >= 0 && ' ' == s[i]; i--) s[i] = 0;
}

/**
 * \brief Detect the most suitable encoding for the string and calculate the length as well
 */
uint8_t fru_get_typelen(int len,             /**< [in] Length of the data or LEN_AUTO for pure text zero-terminated data */
                        const uint8_t *data) /**< [in] The input data */
{
	uint8_t typelen = 0;
	int i;

	if (!data)
		return FRU_FIELD_EMPTY;

	if (!len) {
		len = strlen(data);
		if (!len) {
			return FRU_FIELD_EMPTY;
		}
	}

	// If the data exceeds the maximum length, return a terminator
	if (len > FRU_FIELDDATALEN(len)) {
		DEBUG("Data exceeds maximum length\n");
		return FRU_FIELD_TERMINATOR;
	}

	if (typelen) {
		// They gave us a non-zero length. The data must be binary. Trust 'em and don't try to optimize.
		DEBUG("Binary data due to non-zero length\n");
		return FRU_TYPELEN(BINARY, len);
	}

	// As we reach this point, we know the data must be text.
	// We will try to find the encoding that suits best.

	typelen = FRU_TYPELEN(BCDPLUS, (len + 1) / 2); // By default - the most range-restricted text type

	DEBUG("Assuming BCD plus data...\n");

	// Go through the data and expand charset as needed
	for (i = 0; i < len; i++) {
		if (data[i] < ' ') {
			// They lied! The data is binary!
			// That's the widest range type.
			// There is no use in checking any further.
			DEBUG("[%#02x] Binary data!\n", data[i]);
			typelen = FRU_TYPELEN(BINARY, len);
			break;
		}

		if (typelen < FRU_MAKETYPE(TEXT) && data[i] > '_') { // Do not reduce the range
			// The data doesn't fit into 6-bit ASCII, expand to simple text.
			DEBUG("[%c] Data is simple text!\n", data[i]);
			typelen = FRU_TYPELEN(TEXT, len);
			continue;
		}

		if (typelen < FRU_MAKETYPE(ASCII_6BIT) && // Do not reduce the range
		    !isdigit(data[i]) && data[i] != ' ' && data[i] != '-' && data[i] != '.')
		{
			// The data doesn't fit into BCD plus, expand to 
			DEBUG("[%c] Data is 6-bit ASCII!\n", data[i]);
			typelen = FRU_TYPELEN(ASCII_6BIT, FRU_6BIT_LENGTH(len));
		}
	}

	return typelen;
}

/**
 * Allocate a buffer and encode the input string into it as 6-bit ASCII
 */
static fru_field_t *fru_encode_6bit(const unsigned char *s /**< [in] Input string */)
{
	int len = strlen(s);
	int len6bit = FRU_6BIT_LENGTH(len);
	int i, i6;
	fru_field_t *out = NULL;

	if (len6bit > FRU_FIELDDATALEN(len6bit) ||
	    !(out = malloc(FRU_FIELDSIZE(len6bit) + 1))) // One byte for null-byte
	{
		return out;
	}

	bzero(out->data, FRU_FIELDSIZE(len6bit) + 1);

	out->typelen = FRU_TYPELEN(ASCII_6BIT, len6bit);

	for (i = 0, i6 = 0; i < len && i6 <= len6bit; i++) {
		int base = i / 4; // Four original bytes get encoded into three 6-bit-packed ones
		int byte = i % 4;
		char c = (s[i] - ' ') & 0x3F; // Space is zero, maximum is 0x3F (6 significant bits)

		DEBUG("%d:%d = %c -> %02hhX\n", base, byte, s[i], c);
		switch(byte) {
			case 0:
				out->data[i6] = c;
				break;
			case 1:
				out->data[i6] |= (c & 0x03) << 6; // Lower 2 bits go high into byte 0
				out->data[++i6] = c >> 2;         // Higher (4) bits go low into byte 1
				break;
			case 2:
				out->data[i6++] |= c << 4;    // Lower 4 bits go high into byte 1
				out->data[i6] = c >> 4;       // Higher 2 bits go low into byte 2
				break;
			case 3:
				out->data[i6++] |= c << 2;  // The whole 6-bit char goes high into byte 3
				break;
		}
	}

	return out;
}

/**
 * Allocate a buffer and decode a 6-bit ASCII string into it
 */
static unsigned char *fru_decode_6bit(const fru_field_t *field)
{
	unsigned char *out = NULL;
	const unsigned char *s6;
	int len, len6bit;
	int i, i6;

	if (!field) return out;

	len6bit = FRU_FIELDDATALEN(field->typelen);
	s6 = field->data;

	len = FRU_6BIT_FULLLENGTH(len6bit);
	if (!(out = malloc(len + 1))) {
		return out;
	}
	DEBUG("Allocated a destination buffer at %p\n", out);
	bzero(out, len + 1);

	for(i = 0, i6 = 0; i6 <= len6bit && i < len && s6[i6]; i++) {
		int base = i / 4;
		int byte = i % 4;

		DEBUG("%d:%d:%d = ", base, byte, i6);

		switch(byte) {
			case 0:
				DEBUG("%02hhX ", s6[i6]);
				out[i] = s6[i6] & 0x3F;
				break;
			case 1:
				DEBUG("%02hhX %02hhX ", s6[i6], s6[i6 + 1]);
				out[i] = (s6[i6] >> 6) | (s6[++i6] << 2);
				break;
			case 2:
				DEBUG("%02hhX %02hhX ", s6[i6], s6[i6 + 1]);
				out[i] = (s6[i6] >> 4) | (s6[++i6] << 4);
				break;
			case 3:
				DEBUG("%02hhX ", s6[i6]);
				out[i] = s6[i6++] >> 2;
				break;
		}
		out[i] &= 0x3F;
		out[i] += ' ';
		DEBUG("-> %02hhx %c\n", out[i], out[i]);
	}

	// Strip trailing spaces that could emerge when decoding a
	// string that was a byte shorter than a multiple of 4.
	cut_tail(out);

	return out;
}

/**
 * Allocate a buffer and encode that data as per FRU specification
 */
fru_field_t * fru_encode_data(int len, const uint8_t *data)
{
	int typelen;
	fru_field_t *out;

	typelen = fru_get_typelen(len, data);
	if (FRU_FIELD_TERMINATOR == typelen)
		return NULL; // Can't encode this data

	if (FRU_ISTYPE(typelen, ASCII_6BIT)) {
		out = fru_encode_6bit(data);
	}
	else {
		if (!(out = malloc(FRU_FIELDSIZE(typelen) + 1))) // Plus 1 byte for null-terminator
			return NULL;

		out->typelen = typelen;
		if (FRU_ISTYPE(typelen, BCDPLUS)) {
			int i;
			uint8_t c[2] = {0};

			/* Copy the data and pack it as BCD */
			for (i = 0; i < 2 * FRU_FIELDDATALEN(typelen); i++) {
				switch(data[i]) {
					case 0: // The null-terminator encountered earlier than end of BCD field, encode as space
					case ' ':
						c[i % 2] = 0xA;
						break;
					case '-':
						c[i % 2] = 0xB;
						break;
					case '.':
						c[i % 2] = 0xC;
						break;
					default: // Digits
						c[i % 2] = data[i] - '0';
				}
				out->data[i / 2] = c[0] << 4 | c[1];
			}
		}
		else {
			memcpy(out->data, data, FRU_FIELDDATALEN(typelen));
		}
		out->data[FRU_FIELDDATALEN(typelen)] = 0; // Terminate the string (for safety)
	}

	return out;
}

/**
 * Allocate a buffer and decode the data into it.
 *
 * For binary data use FRU_FIELDDATALEN(field->typelen) to find
 * out the size of the returned buffer.
 */
unsigned char * fru_decode_data(const fru_field_t *field)
{
	unsigned char * out;

	if (!field) return NULL;

	if (FRU_ISTYPE(field->typelen, ASCII_6BIT)) {
		out = fru_decode_6bit(field);
	}
	else {
		out = malloc(FRU_FIELDDATALEN(field->typelen) + 1);
		if (!out) return NULL;

		if (FRU_ISTYPE(field->typelen, BCDPLUS)) {
			int i;
			uint8_t c;
			/* Copy the data and pack it as BCD */
			for (i = 0; i < 2 * FRU_FIELDDATALEN(field->typelen); i++) {
				c = (field->data[i / 2] >> ((i % 2) ? 0 : 4)) & 0x0F;
				switch(c) {
					case 0xA:
						out[i] = ' ';
						break;
					case 0xB:
						out[i] = '-';
						break;
					case 0xC:
						out[i] = '.';
						break;
					default: // Digits
						out[i] = c + '0';
				}
			}
			out[2 * FRU_FIELDDATALEN(field->typelen)] = 0; // Terminate the string
			// Strip trailing spaces that may have emerged when a string of odd
			// length was BCD-encoded.
			cut_tail(out);
		}
		else {
			memcpy(out, field->data, FRU_FIELDDATALEN(field->typelen));
			out[FRU_FIELDDATALEN(field->typelen)] = 0; // Terminate the string
		}
	}

	return out;
}

#if 0
struct timeval {
	time_t      tv_sec;     /* seconds */
	suseconds_t tv_usec;    /* microseconds */
};

	struct timezone {
		int tz_minuteswest;     /* minutes west of Greenwich */
		int tz_dsttime;         /* type of DST correction */
	};

gettimeofday time
#endif

/**
 * Calculate an area checksum
 *
 * Calculation includes the checksum byte itself.
 * For freshly prepared area this method returns a checksum to be stored in the last byte.
 * For a pre-existing area this method returns zero if checksum is ok or non-zero otherwise.
 *
 */
uint8_t fru_area_checksum(fru_sized_area_t *area)
{
	int i;
	uint8_t checksum = 0;
	uint8_t *data = (uint8_t *)area;

	for (i = 0; i < area->blocks * FRU_BLOCK_SZ; i++) {
		checksum += data[i];
	}

	return (uint8_t)( -(int8_t)checksum);
}

/**
 * Allocate and build a FRU Information Area block of any type.
 *
 * The function will allocate a buffer of size that is a muliple of 8 bytes
 * and is big enough to accomodate the standard area header corresponding to the
 * requested area type, as well as all the supplied data fields, the require padding,
 * and a checksum byte.
 *
 * The data fields will be taken as is and should be supplied pre-encoded in
 * the standard FRU field format.
 *
 * It is safe to free (deallocate) the fields supplied to this function
 * immediately after the call as all the data is copied to the new buffer.
 *
 * Don't forget to free() the returned buffer when you don't need it anymore.
 *
 * @returns fru_sized_area_t *area A newly allocated buffer containing the created area
 *
 */
static
fru_sized_area_t *fru_create_sized_area(fru_area_type_t atype,    ///< [in] Area type (FRU_[CHASSIS|BOARD|PRODUCT]_INFO)
                                        uint8_t lang,             ///< [in] Language code for area types that use it (board, product)
                                        const struct timeval *tv, ///< [in] Manufacturing time since the Epoch (1970/01/01 00:00:00 +0000 UTC) for areas that use it (board)
                                        fru_reclist_t *fields)   ///< [in] Single-linked list of data fields
{
	int i;
	int typelen;
	int padding_size;
	fru_board_area_t header = { // Allocate the biggest possible header
		.ver = FRU_VER_1,
	};
	int headerlen = FRU_SIZED_AREA_HEADER_SZ; // Assume a smallest possible header
	void *out = NULL;
	uint8_t *outp;
	fru_reclist_t *field = fields;
	int totalsize = 2; // Assume it's a sized area and so it has size field and a custom fields terminator

	if (!FRU_AREA_HAS_SIZE(atype)) {
		errno = EINVAL; // This function doesn't support multirecord or internal use areas
		goto err;
	}

	if (FRU_AREA_HAS_LANG(atype)) {
		header.lang = lang;
		headerlen = FRU_LANG_AREA_HEADER_SZ; // Expand the header size
	}

	if (FRU_AREA_HAS_DATE(atype)) {
		uint32_t fru_time;
		struct tm tm_1996 = {
			.tm_year = 96,
			.tm_mon = 0,
			.tm_mday = 1
		};
		struct timeval tv_1996 = { 0 };

		if (!tv) {
			errno = EFAULT;
			goto err;
		}

		tv_1996.tv_sec = mktime(&tm_1996); // The argument to mktime is zoneless

		fru_time = (tv->tv_sec - tv_1996.tv_sec) / 60; // FRU time is in minutes and we don't care about microseconds
		header.mfgdate[0] = fru_time         & 0xFF;
		header.mfgdate[1] = (fru_time >> 8)  & 0xFF;
		header.mfgdate[2] = (fru_time >> 16) & 0xFF;
		headerlen = FRU_DATE_AREA_HEADER_SZ; // Expand the header size
	}

	DEBUG("headerlen is %d\n", headerlen);

	totalsize += headerlen;

	for (field = fields; field && field->rec; field = field->next) {
		totalsize += FRU_FIELDSIZE(field->rec->typelen);
	}
	header.blocks = FRU_BLOCKS(totalsize); // Round up to multiple of 8 bytes
	padding_size = header.blocks * FRU_BLOCK_SZ - totalsize;

	out = malloc(FRU_BYTES(header.blocks)); // This will be returned and freed by the caller
	bzero(out, FRU_BYTES(header.blocks));
	outp = out;

	if (!out) goto err;

	// Now fill the output buffer. First copy the header.
	memcpy(outp, &header, headerlen);
	outp += headerlen;
	
	DEBUG("area size is %d (%d) bytes\n", totalsize, FRU_BYTES(header.blocks));
	DEBUG("area size in header is (%d) bytes\n", FRU_BYTES(((fru_sized_area_t *)out)->blocks));

	// Add the data fields
	for (field = fields; field && field->rec; field = field->next) {
		outp += fru_field_copy(outp, field->rec);
	}

	// Terminate the data fields, add padding and checksum
	*outp = FRU_FIELD_TERMINATOR;
	outp += 1 + padding_size;
	*outp = fru_area_checksum(out);

	DEBUG("area size is %d (%d) bytes\n", totalsize, FRU_BYTES(header.blocks));
	DEBUG("area size in header is (%d) bytes\n", FRU_BYTES(((fru_sized_area_t *)out)->blocks));

err:
	return out;
}

/**
 * Allocate and build a Board Information Area block.
 *
 * The function will allocate a buffer of size that is a muliple of 8 bytes
 * and is big enough to accomodate the standard area header, all the mandatory
 * fields, all the supplied custom fields, the required padding and a checksum byte.
 *
 * The mandatory fields will be encoded as fits best.
 * The custom fields will be used as is (pre-encoded).
 *
 * It is safe to free (deallocate) any arguments supplied to this function
 * immediately after the call as all the data is copied to the new buffer.
 *
 * Don't forget to free() the returned buffer when you don't need it anymore.
 *
 * @returns fru_sized_area_t *area A newly allocated buffer containing the created area
 *
 */
fru_board_area_t * fru_board_info(uint8_t lang,                ///< [in] Language code
                                  const struct timeval *tv,    ///< [in] Time since the Epoch (1970/01/01 00:00:00 +0000 UTC)
                                  const unsigned char *mfg,    ///< [in] Manufacturer name
                                  const unsigned char *pname,  ///< [in] Product name
                                  const unsigned char *serial, ///< [in] Serial number
                                  const unsigned char *pn,     ///< [in] Part number
                                  const unsigned char *file,   ///< [in] FRU File ID
                                  fru_reclist_t *cust)         ///< [in] Single-linked list of custom fields
{
	int i;
	fru_reclist_t fields[] = { // List of fields. Mandatory fields are unallocated yet.
		[FRU_BOARD_MFG]      = { NULL, &fields[FRU_BOARD_PRODNAME] },
		[FRU_BOARD_PRODNAME] = { NULL, &fields[FRU_BOARD_SERIAL] },
		[FRU_BOARD_SERIAL]   = { NULL, &fields[FRU_BOARD_PARTNO] },
		[FRU_BOARD_PARTNO]   = { NULL, &fields[FRU_BOARD_FILE] },
		[FRU_BOARD_FILE]     = { NULL, cust },
	};
	fru_reclist_t *field = &fields[0];

	const unsigned char *strings[] = { mfg, pname, serial, pn, file };
	fru_board_area_t *out = NULL;

	int field_count;

	/* Find uninitialized mandatory fields, allocate and count them */
	for (field_count = -1, field = &fields[0]; field && !field->rec; field = field->next) {
		field_count++;
		if (field_count >= ARRAY_SZ(strings))
			break;

		if (!field->rec) {
			field->rec = fru_encode_data(LEN_AUTO, strings[field_count]);
			if (!field->rec) goto err;
		}
	}

	out = (fru_board_area_t *)fru_create_sized_area(FRU_BOARD_INFO, lang, tv, fields);

err:

	/* Free the allocated mandatory field */
	for (; field_count >= 0; field_count--) {
		free(fields[field_count--].rec);
	}

	return out;
}

/**
 * Allocate and build a Product Information Area block.
 *
 * The function will allocate a buffer of size that is a muliple of 8 bytes
 * and is big enough to accomodate the standard area header, all the mandatory
 * fields, all the supplied custom fields, the required padding and a checksum byte.
 *
 * The mandatory fields will be encoded as fits best.
 * The custom fields will be used as is (pre-encoded).
 *
 * It is safe to free (deallocate) any arguments supplied to this function
 * immediately after the call as all the data is copied to the new buffer.
 *
 * Don't forget to free() the returned buffer when you don't need it anymore.
 *
 * @returns fru_sized_area_t *area A newly allocated buffer containing the created area
 *
 */
fru_product_area_t * fru_product_info(uint8_t lang,                ///< [in] Language code
                                      const unsigned char *mfg,    ///< [in] Manufacturer name
                                      const unsigned char *pname,  ///< [in] Product name
                                      const unsigned char *pn,     ///< [in] Part number
                                      const unsigned char *ver,    ///< [in] Product version
                                      const unsigned char *serial, ///< [in] Serial number
                                      const unsigned char *atag,   ///< [in] Asset Tag
                                      const unsigned char *file,   ///< [in] FRU File ID
                                      fru_reclist_t *cust)         ///< [in] Single-linked list of custom fields
{
	int i;
	fru_reclist_t fields[] = { // List of fields. Mandatory fields are unallocated yet.
		[FRU_PROD_MFG]     = { NULL, &fields[FRU_PROD_NAME] },
		[FRU_PROD_NAME]    = { NULL, &fields[FRU_PROD_MODELPN] },
		[FRU_PROD_MODELPN] = { NULL, &fields[FRU_PROD_VERSION] },
		[FRU_PROD_VERSION] = { NULL, &fields[FRU_PROD_SERIAL] },
		[FRU_PROD_SERIAL]  = { NULL, &fields[FRU_PROD_ASSET] },
		[FRU_PROD_ASSET]   = { NULL, &fields[FRU_PROD_FILE] },
		[FRU_PROD_FILE]    = { NULL, cust },
	};
	fru_reclist_t *field = &fields[0];

	const unsigned char *strings[] = { mfg, pname, pn, ver, serial, atag, file };
	fru_product_area_t *out = NULL;

	int field_count;

	/* Find uninitialized mandatory fields, allocate and count them */
	for (field_count = -1, field = &fields[0]; field && !field->rec; field = field->next) {
		field_count++;
		if (field_count >= ARRAY_SZ(strings))
			break;

		if (!field->rec) {
			field->rec = fru_encode_data(LEN_AUTO, strings[field_count]);
			if (!field->rec) goto err;
		}
	}

	out = (fru_product_area_t *)fru_create_sized_area(FRU_PRODUCT_INFO, lang, NULL, fields);

err:

	/* Free the allocated mandatory field */
	for (; field_count >= 0; field_count--) {
		free(fields[field_count--].rec);
	}

	return out;
}
/**
 * Create a FRU information file.
 *
 * @param[in] area  The array of 5 areas, each may be NULL.
 *                  Areas must be given in the FRU order, which is:
 *                  internal use, chassis, board, product, multirecord
 * @param[out] size On success, the size of the newly created FRU information buffer, in 8-byte blocks
 *
 * @returns fru_t * buffer, a newly allocated buffer containing the created FRU information file
 */
fru_t * fru_create(fru_area_t area[FRU_MAX_AREAS], size_t *size)
{
	fru_t fruhdr = { .ver = FRU_VER_1 };
	int totalblocks = FRU_BLOCKS(sizeof(fru_t)); // Start with just the header size
	int area_offsets[FRU_MAX_AREAS] = { // Indices must match values of fru_area_type_t
		offsetof(fru_t, internal),
		offsetof(fru_t, chassis),
		offsetof(fru_t, board),
		offsetof(fru_t, product),
		offsetof(fru_t, multirec)
	};
	fru_t *out = NULL;
	int i;

	// First calculate the total size of the FRU information storage file to be allocated.
	for(i = 0; i < FRU_MAX_AREAS; i++) {
		uint8_t atype = area[i].atype;
		uint8_t blocks = area[i].blocks;
		fru_sized_area_t *data = area[i].data;

		// Area type must be valid and match the index
		if (!FRU_IS_ATYPE_VALID(atype) || atype != (uint8_t)FRU_AREA_NOT_PRESENT && atype != i) {
			errno = EINVAL;
			return NULL;
		}

		int area_offset_index = area_offsets[atype];
		uint8_t *offset = (uint8_t *)&fruhdr + area_offset_index;

		if(!data ||                                // No data is provided or
		   !FRU_AREA_HAS_SIZE(atype) && !blocks || // no size is given for a non-sized area or
		   !((fru_sized_area_t *)data)->blocks     // the sized area contains a zero size
		  ) {
			// Mark the area as
			*offset = 0;
			continue;
		}

		if(!blocks) {
			blocks = data->blocks;
			area[i].blocks = blocks;
		}

		*offset = totalblocks;
		totalblocks += blocks;
	}

	out = malloc(FRU_BYTES(totalblocks));

	DEBUG("alocated a buffer at %p\n", out);
	if (!out) return NULL;
	
	bzero(out, FRU_BYTES(totalblocks));

	memcpy(out, (uint8_t *)&fruhdr, sizeof(fruhdr));

	// Now go through the areas again and copy them into the allocated buffer.
	// We have all the offsets and sizes set in the previous loop.
	for(i = 0; i < FRU_MAX_AREAS; i++) {
		uint8_t atype = area[i].atype;
		uint8_t blocks = area[i].blocks;
		uint8_t *data = area[i].data;
		int area_offset_index = area_offsets[atype];
		uint8_t *offset = (uint8_t *)&fruhdr + area_offset_index;
		uint8_t *dst = (void *)out + FRU_BYTES(*offset);

		if (!blocks) continue;

		DEBUG("copying %d bytes of area of type %d to offset 0x%03X (0x%03lX)\n",
		      FRU_BYTES(blocks), atype, FRU_BYTES(*offset), dst - (uint8_t *)out
		      );
		memcpy(dst, data, FRU_BYTES(blocks));
	}

	*size = totalblocks;
	return out;
}


#ifdef __STANDALONE__

void dump(int len, const unsigned char *data)
{
	int i;
	printf("Data Dump:");
	for (i = 0; i < len; i++) {
		if (!(i % 16)) printf("\n%04X:  ", i);
		printf("%02X ", data[i]);
	}
	printf("\n");
}

void test_encodings(void)
{
	int i, len;
	uint8_t typelen;
	unsigned char *test_strings[] = {
		/* 6-bit ASCII */
		"IPMI", "OK!",
		/* BCD plus */
		"1234-56-7.89 01",
		/* Simple text */
		"This is a simple text, with punctuation & other stuff",
		/* Binary */
		"\x00\x01\x02\x03\x04\x05 BINARY TEST"
	};
	unsigned char *test_types[] = {
		"6-bit", "6-bit",
		"BCPplus",
		"Simple text",
		"Binary"
	};
	int test_lengths[] = { LEN_AUTO, LEN_AUTO, LEN_AUTO, LEN_AUTO, 18 };

	for(i = 0; i < ARRAY_SZ(test_strings); i++) {
		fru_field_t *field;
		const unsigned char *out;

		printf("Data set %d.\n", i);
		printf("Original data ");
		if (test_lengths[i]) dump(test_lengths[i], test_strings[i]);
		else printf(": [%s]\n", test_strings[i]);

		printf("Original type: %s\n", test_types[i]);
		printf("Encoding... ");
		field = fru_encode_data(test_lengths[i], test_strings[i]);
		if (FRU_FIELD_TERMINATOR == field->typelen) {
			printf("FAIL!\n\n");
			continue;
		}

		printf("OK\n");
		printf("Encoded type is: ");
		switch((field->typelen & __TYPE_BITS_MASK) >> __TYPE_BITS_SHIFT) {
			case __TYPE_TEXT:
				printf("Simple text\n");
				break;
			case __TYPE_ASCII_6BIT:
				printf("6-bit\n");
				break;
			case __TYPE_BCDPLUS:
				printf("BCDplus\n");
				break;
			default:
				printf("Binary\n");
				break;
		}

		printf("Encoded data ");
		dump(FRU_FIELDSIZE(field->typelen), (uint8_t *)field);
		printf("Decoding... ");

		out = fru_decode_data(field);
		if (!out) {
			printf("FAIL!");
			goto next;
		}

		printf("Decoded data ");
		if (FRU_ISTYPE(field->typelen, BINARY)) {
			dump(FRU_FIELDDATALEN(field->typelen), out);
		}
		else {
			printf(": [%s]\n", out);
		}

		printf("Comparing... ");
		if (test_lengths[i] && !memcmp(test_strings[i], out, test_lengths[i]) ||
		    !strcmp(test_strings[i], out))
		{
			printf("OK!");
		}
		else {
			printf("FAIL!");
		}

		free((void *)out);
next:
		free((void *)field);
		printf("\n\n");
	}
}

int main(int argc, char *argv[])
{
	fru_board_area_t *bi;
	fru_product_area_t *pi;
	struct timeval now;
	fru_reclist_t *cust = NULL;
#if 0
	test_encodings();
	exit(1);
#else
	tzset();
	gettimeofday(&now, NULL);
	now.tv_sec += timezone;

	int fd, fd2;


	bi = fru_board_info(LANG_ENGLISH,
	                    &now,
	                    "FASTWEL Group Co. Ltd.",
	                    "CPC503 Cool Device",
	                    "25160123",
	                    "CPC503",
	                    "",
	                    cust
	                    );

	printf("Board area checksum is ");
	if (!fru_area_checksum((fru_sized_area_t *)bi)) {
		printf("OK\n");
	}
	else {
		printf("WRONG\n");
	}

	pi = fru_product_info(LANG_ENGLISH,
	                      "FASTWEL Group Co. Ltd.",
	                      "CPC503 Product",
	                      "CPC503",
	                      "1.2",
	                      "25161256",
	                      "Home office",
	                      "file2.dat",
	                      NULL);
	printf("Product area checksum is ");
	if (!fru_area_checksum((fru_sized_area_t *)pi)) {
		printf("OK\n");
	}
	else {
		printf("WRONG\n");
	}

	fru_t *fru;
	size_t size;
	fru_area_t areas[FRU_MAX_AREAS] = {
		{ .atype = FRU_INTERNAL_USE },
		{ .atype = FRU_CHASSIS_INFO },
		{ .atype = FRU_BOARD_INFO, .data = bi },
		{ .atype = FRU_PRODUCT_INFO, .data = pi },
		{ .atype = FRU_MULTIRECORD }
	};
	fru = fru_create(areas, &size);
	if (!fru) {
		perror("fru_create");
		goto err;
		exit(1);
	}

	printf("Writing %lu bytes of FRU data.\n", FRU_BYTES(size));
	
	fd = open("frux.bin", O_CREAT | O_TRUNC | O_WRONLY, 0644);

	if (fd < 0)
		perror("open");

	if (0 > write(fd, fru, FRU_BYTES(size)))
		perror("write2");

	free(fru);

err:
	free(pi);
	free(bi);
#endif
}
#endif
