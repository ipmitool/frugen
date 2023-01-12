/** @file
 *  @brief FRU information encoding functions
 *
 *  Copyright (C) 2016-2021 Alexander Amelkin <alexander@amelkin.msk.ru>
 *  SPDX-License-Identifier: LGPL-2.0-or-later OR Apache-2.0
 */

#include "fru.h"
#include "smbios.h"

#include <ctype.h>
#include <stdio.h>
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

#define _BSD_SOURCE
#include <endian.h>

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

static bool autodetect = true;

const char* enc_names[TOTAL_FIELD_TYPES] = {
	[FIELD_TYPE_AUTO] = "auto",
	[FIELD_TYPE_BINARY] = "binary",
	[FIELD_TYPE_BCDPLUS] = "bcdplus",
	[FIELD_TYPE_SIXBITASCII] = "6bitascii",
	[FIELD_TYPE_TEXT] = "text"
};

void fru_set_autodetect(bool enable)
{
	autodetect = enable;
}

/**
 * Get the FRU date/time base in seconds since UNIX Epoch
 *
 * According to IPMI FRU Information Storage Definition v1.0, rev 1.3,
 * the date/time encoded as zero designates "0:00 hrs 1/1/96",
 * see Table 11-1 "BOARD INFO AREA"
 *
 * @returns The number of seconds from UNIX Epoch to the FRU date/time base
 */
static time_t fru_datetime_base() {
	struct tm tm_1996 = {
		.tm_year = 96,
		.tm_mon = 0,
		.tm_mday = 1
	};
	// The argument to mktime is zoneless
	return mktime(&tm_1996);
}


/**
 * Strip trailing spaces
 */
static inline void cut_tail(char *s)
{
	int i;
	for(i = strlen(s) - 1; i >= 0 && ' ' == s[i]; i--) s[i] = 0;
}

/** Copy a FRU area field to a buffer and return the field's size */
static inline uint8_t fru_field_copy(void *dest, const fru_field_t *fieldp)
{
	memcpy(dest, (void *)fieldp, FRU_FIELDSIZE(fieldp->typelen));
	return FRU_FIELDSIZE(fieldp->typelen);
}

/**
 * Detect the most suitable encoding for the string and calculate the length as well
 *
 * @returns A FRU field type/length byte, as per IPMI FRU Storage Definition, if everything was ok, or an error code.
 * @retval FRU_FIELD_EMPTY The \a data argument was NULL
 * @retval FRU_FIELD_TERMINATOR The data exceeded the maximum length (63 bytes)
 *
 */
static
uint8_t fru_get_typelen(int len,             /**< [in] Length of the data,
						LEN_AUTO for pure text zero-terminated data or
						one of LEN_BCDPLUS, LEN_6BITASCII, LEN_TEXT for explicit text type */
                        const uint8_t *data) /**< [in] The input data */
{
	uint8_t typelen = len;
	int i;

	if (!data)
		return FRU_FIELD_EMPTY;

	if (len < 0) {
		DEBUG("Forcing string '%s' to ...\n", (char *)data);
		// Explicit text type
                if (len == LEN_BCDPLUS) {
			DEBUG("BCDPLUS type\n");
			return FRU_TYPELEN(BCDPLUS, (strlen(data) + 1) / 2);
                } else if (len == LEN_6BITASCII) {
			DEBUG("6BIT ASCII type\n");
			return FRU_TYPELEN(ASCII_6BIT, FRU_6BIT_LENGTH(strlen(data)));
                } else if (len == LEN_TEXT) {
			DEBUG("ASCII type\n");
			return FRU_TYPELEN(TEXT, strlen(data));
                } else {
			DEBUG("Nothing... Unknown text type\n");
			return FRU_FIELD_TERMINATOR;
		}
	}

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

	DEBUG("Guessing type of string '%s'...\n", (char *)data);

	// As we reach this point, we know the data must be text.
	// We will try to find the encoding that suits best.
	if (autodetect) {
		typelen = FRU_TYPELEN(BCDPLUS, (len + 1) / 2); // By default - the most range-restricted text type

		DEBUG("Assuming BCD plus data...\n");
	}
	else {
		DEBUG("Assuming ASCII data...\n");
		typelen = FRU_TYPELEN(TEXT, len);
	}

	// Go through the data and expand charset as needed
	for (i = 0; i < len; i++) {
		if (data[i] < ' '
			&& data[i] != '\t'
			&& data[i] != '\r'
			&& data[i] != '\n')
		{
			// They lied! The data is binary!
			// That's the widest range type.
			// There is no use in checking any further.
			DEBUG("[%#02x] Binary data!\n", data[i]);
			typelen = FRU_TYPELEN(BINARY, len);
			break;
		}

		if (autodetect) {
			if (typelen < FRU_MAKETYPE(TEXT)
				&& (data[i] > '_' || data[i] < ' '))
			{ // Do not reduce the range
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
		} /* autodetect */
	}

	return typelen;
}

/**
 * Allocate a buffer and encode the input string into it as 6-bit ASCII
 *
 * @returns pointer to the newly allocated field buffer if allocation and encoding were successful
 * @returns NULL if there was an error, sets errno accordingly (man malloc)
 */
static fru_field_t *fru_encode_6bit(const unsigned char *s /**< [in] Input string */)
{
	int len = strlen(s);
	int len6bit = FRU_6BIT_LENGTH(len);
	int i, i6;
	fru_field_t *out = NULL;
	size_t outlen = sizeof(fru_field_t) + len6bit + 1; // 1 extra for null-byte

	if (len6bit > FRU_FIELDDATALEN(len6bit) ||
	    !(out = calloc(1, outlen)))
	{
		return out;
	}

	out->typelen = FRU_TYPELEN(ASCII_6BIT, len6bit);

	for (i = 0, i6 = 0; i < len && i6 < len6bit; i++) {
		int base = i / 4; // Four original bytes get encoded into three 6-bit-packed ones
		int byte = i % 4;
		char c = (s[i] - ' ') & 0x3F; // Space is zero, maximum is 0x3F (6 significant bits)

		DEBUG("%d:%d:%d = %c -> %02hhX\n", base, byte, i6, s[i], c);
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
 * @brief Decode a 6-bit ASCII string.
 *
 * @param[in] field Field to decode.
 * @param[out] out Buffer to decode into.
 * @param[in] out_len Length of output buffer.
 * @retval true Success.
 * @retval false Failure.
 */
static
bool fru_decode_6bit(const fru_field_t *field,
		     uint8_t *out,
		     size_t out_len)
{
	const unsigned char *s6;
	int len, len6bit;
	int i, i6;

	if (!field) return false;

	len6bit = FRU_FIELDDATALEN(field->typelen);
	s6 = field->data;

	len = FRU_6BIT_FULLLENGTH(len6bit);
	if (out_len < (len + 1)) {
		return false;
	}

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

	return true;
}

/**
 * @brief Decode BCDPLUS string.
 *
 * @param[in] field Field to decode.
 * @param[out] out Buffer to decode into.
 * @param[in] out_len Length of output buffer.
 * @retval true Success.
 * @retval false Failure.
 */
static
bool fru_decode_bcdplus(const fru_field_t *field,
			uint8_t *out,
			size_t out_len)
{
	int i;
	uint8_t c;
	if (out_len < 2 * FRU_FIELDDATALEN(field->typelen) + 1)
		return false;
	/* Copy the data and pack it as BCD */
	for (i = 0; i < 2 * FRU_FIELDDATALEN(field->typelen); i++) {
		c = (field->data[i / 2] >> ((i % 2) ? 0 : 4)) & 0x0F;
		switch (c) {
		case 0xA:
			out[i] = ' ';
			break;
		case 0xB:
			out[i] = '-';
			break;
		case 0xC:
			out[i] = '.';
			break;
		case 0xD:
		case 0xE:
		case 0xF:
			out[i] = '?';
			break;
		default: // Digits
			out[i] = c + '0';
		}
	}
	out[2 * FRU_FIELDDATALEN(field->typelen)] = 0; // Terminate the string
	// Strip trailing spaces that may have emerged when a string of odd
	// length was BCD-encoded.
	cut_tail(out);

	return true;
}

/**
 * @brief Get a hex string representation of the supplied binary field.
 *
 * @param[in] field Field to decode.
 * @param[out] out Buffer to decode into.
 * @param[in] out_len Length of output buffer.
 * @retval true Success.
 * @retval false Failure.
 */
static
bool fru_decode_binary(const fru_field_t *field,
		       uint8_t *out,
		       size_t out_len)
{
	int i;
	uint8_t c;

	if ((FRU_FIELDDATALEN(field->typelen) * 2 + 1) > out_len)
		return false;

	for (i = 0; i < FRU_FIELDDATALEN(field->typelen); i++) {
		c = (field->data[i] & 0xf0) >> 4;
		out[2 * i] = c > 9? c - 10 + 'A': c + '0';
		c = field->data[i] & 0xf;
		out[2 * i + 1] = c > 9? c - 10 + 'A': c + '0';
	}
	out[i * 2 + 1] = '0';

	return true;
}

/**
 * @brief Allocate a buffer and encode that data as per FRU specification
 *
 * @param[in] len Buffer length.
 * @param[in] data Binary buffer.
 * @param[in] out_len Length of output buffer.
 * @retval NULL Failure.
 * @return Encoded field.
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

bool fru_decode_data(fru_field_t *field,
		     typed_field_t *out,
		     size_t out_len)
{
	if (!field) return false;

	if (FRU_ISTYPE(field->typelen, ASCII_6BIT)) {
		out->type = FIELD_TYPE_SIXBITASCII;
		return fru_decode_6bit(field, out->val, out_len);
	} else {
		if (out_len < (FRU_FIELDDATALEN(field->typelen) + 1))
			return false;

		if (FRU_ISTYPE(field->typelen, BCDPLUS)) {
			out->type = FIELD_TYPE_BCDPLUS;
			return fru_decode_bcdplus(field, out->val, out_len);
		} else {
			out->type = FIELD_TYPE_TEXT;
			memcpy(out->val, field->data, FRU_FIELDDATALEN(field->typelen));
			out->val[FRU_FIELDDATALEN(field->typelen)] = 0; // Terminate the string
			return true;
		}
	}
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
 * Calculate zero checksum for command header and FRU areas
 */
static
uint8_t calc_checksum(void *blk, size_t blk_bytes)
{
	if (!blk || blk_bytes == 0) {
		printf("Null pointer or zero buffer length\n");
		exit(1);
	}

	uint8_t *data = (uint8_t *)blk;
	uint8_t checksum = 0;

	for(int i = 0; i < blk_bytes; i++) {
		checksum += data[i];
	}

	return (uint8_t)( -(int8_t)checksum);
}

/**
 * Calculate an area checksum
 *
 * Calculation includes the checksum byte itself.
 * For freshly prepared area this method returns a checksum to be stored in the last byte.
 * For a pre-existing area this method returns zero if checksum is ok or non-zero otherwise.
 *
 */
uint8_t fru_area_checksum(fru_info_area_t *area)
{
	return calc_checksum(area, (area->blocks * FRU_BLOCK_SZ));
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
 * @returns fru_info_area_t *area A newly allocated buffer containing the created area
 *
 */
static
fru_info_area_t *fru_create_info_area(fru_area_type_t atype,    ///< [in] Area type (FRU_[CHASSIS|BOARD|PRODUCT]_INFO)
				      uint8_t langtype,         ///< [in] Language code for areas that use it (board, product) or Chassis Type for chassis info area
				      const struct timeval *tv, ///< [in] Manufacturing time since the Epoch (1970/01/01 00:00:00 +0000 UTC) for areas that use it (board)
				      fru_reclist_t *fields,   ///< [in] Single-linked list of data fields
				      size_t nstrings,         ///< [in] Number of strings for mandatory fields
				      const typed_field_t strings[]) ///<[in] Array of typed strings for mandatory fields
{
	int i = 0;
	int field_count;
	int typelen;
	int padding_size;
	fru_board_area_t header = { // Allocate the biggest possible header
		.ver = FRU_VER_1,
	};
	int headerlen = FRU_INFO_AREA_HEADER_SZ; // Assume a smallest possible header for a generic info area
	void *out = NULL;
	uint8_t *outp;
	fru_reclist_t *field = fields;
	int totalsize = 2; // A generic info area has a custom fields terminator and a checksum

	if (!FRU_AREA_IS_GENERIC(atype)) {
		errno = EINVAL; // This function doesn't support multirecord or internal use areas
		goto err;
	}

	header.langtype = langtype;

	if (FRU_AREA_HAS_DATE(atype)) {
		uint32_t fru_time;
		const struct timeval tv_unspecified = { 0 };

		if (!tv) {
			errno = EFAULT;
			goto err;
		}

		/*
		 * It's assumed here that UNIX time 0 (Jan 1st of 1970)
		 * can never actually happen in a FRU file in 2018.
		 */
		if (!memcmp(&tv_unspecified, tv, sizeof(tv))) {
			printf("Using FRU_DATE_UNSPECIFIED\n");
			fru_time = FRU_DATE_UNSPECIFIED;
		} else {
			// FRU time is in minutes and we don't care about microseconds
			fru_time = (tv->tv_sec - fru_datetime_base()) / 60;
		}
		header.mfgdate[0] = fru_time         & 0xFF;
		header.mfgdate[1] = (fru_time >> 8)  & 0xFF;
		header.mfgdate[2] = (fru_time >> 16) & 0xFF;
		headerlen = FRU_DATE_AREA_HEADER_SZ; // Expand the header size
	}

	DEBUG("headerlen is %d\n", headerlen);

	totalsize += headerlen;

	/* Find uninitialized mandatory fields, allocate and initialize them with provided strings */
	for (field_count = 0, field = fields;
	     field && !field->rec && field_count < nstrings;
	     field = field->next, field_count++)
	{
		int len = LEN_AUTO;
		switch (strings[field_count].type) {
			case FIELD_TYPE_BINARY: {
				DEBUG("binary format not yet implemented\n");
				errno = EINVAL;
				goto err;
			}
			case FIELD_TYPE_BCDPLUS: {
				len = LEN_BCDPLUS;
				break;
			}
			case FIELD_TYPE_SIXBITASCII: {
				len = LEN_6BITASCII;
				break;
			}
			case FIELD_TYPE_TEXT: {
				len = LEN_TEXT;
				break;
			}
			default:
				len = LEN_AUTO;
				break;
		}
		field->rec = fru_encode_data(len, strings[field_count].val);
		if (!field->rec) goto err;
	}

	/* Now calculate the total size of all initialized (mandatory and custom) fields */
	for (field = &fields[0]; field && field->rec; field = field->next) {
		totalsize += FRU_FIELDSIZE(field->rec->typelen);
	}

	header.blocks = FRU_BLOCKS(totalsize); // Round up to multiple of 8 bytes
	padding_size = header.blocks * FRU_BLOCK_SZ - totalsize;

	out = calloc(1, FRU_BYTES(header.blocks)); // This will be returned and freed by the caller
	outp = out;

	if (!out) goto err;

	// Now fill the output buffer. First copy the header.
	memcpy(outp, &header, headerlen);
	outp += headerlen;

	DEBUG("area size is %d (%d) bytes\n", totalsize, FRU_BYTES(header.blocks));
	DEBUG("area size in header is (%d) bytes\n", FRU_BYTES(((fru_info_area_t *)out)->blocks));

	// Add the data fields
	for (field = fields; field && field->rec; field = field->next) {
		outp += fru_field_copy(outp, field->rec);
	}

	// Terminate the data fields, add padding and checksum
	*outp = FRU_FIELD_TERMINATOR;
	outp += 1 + padding_size;
	*outp = fru_area_checksum(out);

	DEBUG("area size is %d (%d) bytes\n", totalsize, FRU_BYTES(header.blocks));
	DEBUG("area size in header is (%d) bytes\n", FRU_BYTES(((fru_info_area_t *)out)->blocks));

err:
	/*
	 * Free the allocated mandatory fields. Either an error has occured or the fields
	 * have already been copied into the output buffer. Anyway, they aren't needed anymore
	 */
	for (--field_count; field_count >= 0; field_count--) {
		free(fields[field_count].rec);
	}
	return out;
}

static bool fru_decode_custom_fields(const uint8_t *data, fru_reclist_t **reclist) {
	fru_field_t *field = NULL;

	while (true) {
		field = (fru_field_t*)data;

		// end of fields
		if (field->typelen == FRU_TYPE_EOF)
			break;

		fru_reclist_t *custom_field = add_reclist(reclist);
		if (custom_field == NULL)
			return false;

		size_t length = FRU_FIELDDATALEN(field->typelen);
		custom_field->rec = calloc(1, FRU_FIELDMAXARRAY);
		custom_field->rec->typelen = field->typelen;
		switch (FRU_TYPE(field->typelen)) {
			case __TYPE_BINARY:
				fru_decode_binary(field, custom_field->rec->data, FRU_FIELDMAXLEN);
				break;
			case __TYPE_ASCII_6BIT:
				fru_decode_6bit(field, custom_field->rec->data, FRU_FIELDMAXLEN);
				break;
			case __TYPE_BCDPLUS:
				fru_decode_bcdplus(field, custom_field->rec->data, FRU_FIELDMAXLEN);
				break;
			default:
				memcpy(custom_field->rec->data, field->data, length);
				custom_field->rec->data[length] = 0; // Terminate the string
				break;
		}
		data += length + 1;
	}

	return true;
}

/**
 * Allocate and build a Chassis Information Area block.
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
 * @returns fru_info_area_t *area A newly allocated buffer containing the created area
 *
 */
fru_chassis_area_t * fru_encode_chassis_info(const fru_exploded_chassis_t *chassis) ///< [in] Exploded chassis info area
{
	int i;

	if(!chassis) {
		errno = EFAULT;
		return NULL;
	}

	fru_reclist_t fields[] = { // List of fields. Mandatory fields are unallocated yet.
		[FRU_CHASSIS_PARTNO] = { NULL, &fields[FRU_CHASSIS_SERIAL] },
		[FRU_CHASSIS_SERIAL] = { NULL, chassis->cust },
	};

	const typed_field_t strings[] = { chassis->pn, chassis->serial };
	fru_chassis_area_t *out = NULL;

	if (!SMBIOS_CHASSIS_IS_VALID(chassis->type)) {
		errno = EINVAL;
		return NULL;
	}

	out = fru_create_info_area(FRU_CHASSIS_INFO,
	                           chassis->type, NULL, fields,
	                           ARRAY_SZ(strings), strings);

	return out;
}

bool fru_decode_chassis_info(
    const fru_chassis_area_t *area,
    fru_exploded_chassis_t *chassis_out
)
{
	chassis_out->type = area->langtype;
	const uint8_t *data = area->data;
	fru_field_t *field = (fru_field_t*)data;

	if(!fru_decode_data(field, &chassis_out->pn,
			    sizeof(chassis_out->pn.val)))
		return false;

	data += FRU_FIELDSIZE(field->typelen);
	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &chassis_out->serial,
			     sizeof(chassis_out->serial.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	fru_decode_custom_fields(data, &chassis_out->cust);

	return true;
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
 * @returns fru_info_area_t *area A newly allocated buffer containing the created area
 *
 */
fru_board_area_t * fru_encode_board_info(const fru_exploded_board_t *board) ///< [in] Exploded board information area
{
	int i;

	if(!board) {
		errno = EFAULT;
		return NULL;
	}

	fru_reclist_t fields[] = { // List of fields. Mandatory fields are unallocated yet.
		[FRU_BOARD_MFG]      = { NULL, &fields[FRU_BOARD_PRODNAME] },
		[FRU_BOARD_PRODNAME] = { NULL, &fields[FRU_BOARD_SERIAL] },
		[FRU_BOARD_SERIAL]   = { NULL, &fields[FRU_BOARD_PARTNO] },
		[FRU_BOARD_PARTNO]   = { NULL, &fields[FRU_BOARD_FILE] },
		[FRU_BOARD_FILE]     = { NULL, board->cust },
	};

	const typed_field_t strings[] = { board->mfg, board->pname, board->serial, board->pn, board->file };
	fru_board_area_t *out = NULL;

	out = (fru_board_area_t *)fru_create_info_area(FRU_BOARD_INFO,
	                                               board->lang, &board->tv, fields,
	                                               ARRAY_SZ(strings), strings);

	return out;
}

bool fru_decode_board_info(const fru_board_area_t *area,
			   fru_exploded_board_t *board_out)
{
	fru_field_t *field;
	const uint8_t *data = area->data;

	board_out->lang = area->langtype;

	// NOTE: host is not always little endian! //
	union {
		uint32_t val;
		uint8_t arr[4];
	} min_since_1996_big_endian = { 0 };
	min_since_1996_big_endian.arr[1] = area->mfgdate[2];
	min_since_1996_big_endian.arr[2] = area->mfgdate[1];
	min_since_1996_big_endian.arr[3] = area->mfgdate[0];
	uint32_t min_since_1996 = be32toh(min_since_1996_big_endian.val);
	// The argument to mktime is zoneless
	board_out->tv.tv_sec = fru_datetime_base() + 60 * min_since_1996;

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &board_out->mfg,
			     sizeof(board_out->mfg.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &board_out->pname,
			     sizeof(board_out->pname.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &board_out->serial,
			     sizeof(board_out->serial.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &board_out->pn,
			     sizeof(board_out->pn.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &board_out->file,
			     sizeof(board_out->file.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	fru_decode_custom_fields(data, &board_out->cust);

	return true;
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
 * @returns fru_info_area_t *area A newly allocated buffer containing the created area
 *
 */
fru_product_area_t * fru_product_info(const fru_exploded_product_t *product) ///< [in] Exploded product information area
{
	int i;

	if(!product) {
		errno = EFAULT;
		return NULL;
	}

	fru_reclist_t fields[] = { // List of fields. Mandatory fields are unallocated yet.
		[FRU_PROD_MFG]     = { NULL, &fields[FRU_PROD_NAME] },
		[FRU_PROD_NAME]    = { NULL, &fields[FRU_PROD_MODELPN] },
		[FRU_PROD_MODELPN] = { NULL, &fields[FRU_PROD_VERSION] },
		[FRU_PROD_VERSION] = { NULL, &fields[FRU_PROD_SERIAL] },
		[FRU_PROD_SERIAL]  = { NULL, &fields[FRU_PROD_ASSET] },
		[FRU_PROD_ASSET]   = { NULL, &fields[FRU_PROD_FILE] },
		[FRU_PROD_FILE]    = { NULL, product->cust },
	};

	const typed_field_t strings[] = { product->mfg, product->pname,
	                                  product->pn, product->ver,
	                                  product->serial, product->atag,
	                                  product->file };
	fru_product_area_t *out = NULL;

	out = fru_create_info_area(FRU_PRODUCT_INFO,
	                           product->lang, NULL, fields,
	                           ARRAY_SZ(strings), strings);

	return out;
}

/**
 * Take an input string, check that it looks like UUID, and pack it into
 * an "exploded" multirecord area record in binary form.
 *
 * @returns An errno-like negative error code
 * @retval 0        Success
 * @retval EINVAL   Invalid UUID string (wrong length, wrong symbols)
 * @retval EFAULT   Invalid pointer
 * @retval >0       any other error that calloc() is allowed to retrun
 */
int fru_mr_uuid2rec(fru_mr_rec_t **rec, const unsigned char *str)
{
	size_t len;
	fru_mr_mgmt_rec_t *mgmt = NULL;

	const int UUID_SIZE = 16;
	const int UUID_STRLEN_NONDASHED = UUID_SIZE * 2; // 2 hex digits for byte
	const int UUID_STRLEN_DASHED = UUID_STRLEN_NONDASHED + 4;

	union __attribute__((packed)) {
		uint8_t raw[UUID_SIZE];
		// The structure is according to DMTF SMBIOS 3.2 Specification
		struct __attribute__((packed)) {
			// All words and dwords here must be Little-Endian for SMBIOS
			uint32_t time_low;
			uint16_t time_mid;
			uint16_t time_hi_and_version;
			uint8_t clock_seq_hi_and_reserved;
			uint8_t clock_seq_low;
			uint8_t node[6];
		};
	} uuid;

	// Need a valid non-allocated record pointer and a string
	if (!rec || *rec) return -EFAULT;
	if (!str) return -EFAULT;

	len = strlen(str);
	if(UUID_STRLEN_DASHED != len && UUID_STRLEN_NONDASHED != len) {
		return -EINVAL;
	}

	mgmt = calloc(1, sizeof(fru_mr_mgmt_rec_t) + UUID_SIZE);
	if (!mgmt) return errno;

	mgmt->hdr.type_id = FRU_MR_MGMT_ACCESS;
	mgmt->hdr.eol_ver = FRU_MR_VER;
	mgmt->hdr.len = UUID_SIZE + 1; // Include the subtype byte
	mgmt->subtype = FRU_MR_MGMT_SYS_UUID;
	while(*str) {
		static size_t i = 0;
		int val;

		// Skip dashes
		if ('-' == *str) {
			++str;
			continue;
		}

		if (!isxdigit(*str)) {
			free(mgmt);
			return -EINVAL;
		}

		val = toupper(*str);
		if (val < 'A')
			val = val - '0';
		else
			val = val - 'A' + 0xA;

		if (0 == i % 2)
			uuid.raw[i / 2] = val << 4;
		else
			uuid.raw[i / 2] |= val;

		++i;
		++str;
	}

	// Ensure Little-Endian encoding for SMBIOS specification compatibility
	uuid.time_low = htole32(be32toh(uuid.time_low));
	uuid.time_mid = htole16(be16toh(uuid.time_mid));
	uuid.time_hi_and_version = htole16(be16toh(uuid.time_hi_and_version));
	memcpy(mgmt->data, uuid.raw, UUID_SIZE);

	*rec = (fru_mr_rec_t *)mgmt;

	// Checksum the data
	mgmt->hdr.rec_checksum = calc_checksum((*rec)->data, mgmt->hdr.len);

	// Checksum the header, don't include the checksum byte itself
	mgmt->hdr.hdr_checksum = calc_checksum(*rec, sizeof(fru_mr_header_t) - 1);
	return 0;
}

/**
 * Allocate a new multirecord reclist entry and add it to \a reclist,
 * set \a reclist to point to the newly allocated entry if
 * \a reclist was NULL.
 *
 * @returns Pointer to the added entry
 */
fru_mr_reclist_t *add_mr_reclist(fru_mr_reclist_t **reclist)
{
	fru_mr_reclist_t *rec;
	fru_mr_reclist_t *reclist_ptr = *reclist;
	rec = calloc(1, sizeof(*rec));
	if(!rec) return NULL;

	// If the reclist is empty, update it
	if(!reclist_ptr) {
		*reclist = rec;
	} else {
		// If the reclist is not empty, find the last entry and append the new one as next
		while(reclist_ptr->next)
			reclist_ptr = reclist_ptr->next;

		reclist_ptr->next = rec;
	}

	return rec;
}

/**
 * Allocate and build a MultiRecord area block.
 *
 * The function will allocate a buffer of size that is required to store all
 * the provided data and accompanying record headers. It will calculate data
 * and header checksums automatically.
 *
 * All data will be copied as-is, without any additional encoding.
 *
 * It is safe to free (deallocate) any arguments supplied to this function
 * immediately after the call as all the data is copied to the new buffer.
 *
 * Don't forget to free() the returned buffer when you don't need it anymore.
 *
 * @returns fru_mr_area_t *area  A newly allocated buffer containing the created area
 *
 */
fru_mr_area_t *fru_mr_area(fru_mr_reclist_t *reclist, size_t *total)
{
	fru_mr_area_t *area = NULL;
	fru_mr_rec_t *rec;
	fru_mr_reclist_t *listitem = reclist;

	// Calculate the cumulative size of all records
	while (listitem && listitem->rec && listitem->rec->hdr.len) {
		*total += sizeof(fru_mr_header_t);
		*total += listitem->rec->hdr.len;
		listitem = listitem->next;
	}

	area = calloc(1, *total);
	if (!area) {
		*total = 0;
		return NULL;
	}

	// Walk the input records and pack them into an MR area
	listitem = reclist;
	rec = area;
	while (listitem && listitem->rec && listitem->rec->hdr.len) {
		size_t rec_sz = sizeof(fru_mr_header_t) + listitem->rec->hdr.len;
		memcpy(rec, listitem->rec, rec_sz);
		if (!listitem->next) {
			// Update the header and its checksum. Don't include the
			// checksum byte itself.
			size_t checksum_span = sizeof(fru_mr_header_t) - 1;
			rec->hdr.eol_ver |= FRU_MR_EOL;
			rec->hdr.hdr_checksum = calc_checksum(rec, checksum_span);
		}
		rec = (void *)rec + rec_sz;
		listitem = listitem->next;
	}

	return area;
}

bool fru_decode_product_info(
    const fru_product_area_t *area,
    fru_exploded_product_t *product_out
)
{
	fru_field_t *field;
	const uint8_t *data = area->data;

	product_out->lang = area->langtype;

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &product_out->mfg,
			     sizeof(product_out->mfg.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &product_out->pname,
			     sizeof(product_out->pname.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &product_out->pn,
			     sizeof(product_out->pn.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &product_out->ver,
			     sizeof(product_out->ver.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &product_out->serial,
			     sizeof(product_out->serial.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &product_out->atag,
			     sizeof(product_out->atag.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	field = (fru_field_t*)data;
	if (!fru_decode_data(field, &product_out->file,
			     sizeof(product_out->file.val)))
		return false;
	data += FRU_FIELDSIZE(field->typelen);

	fru_decode_custom_fields(data, &product_out->cust);

	return true;
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
		fru_info_area_t *data = area[i].data;

		// Area type must be valid and match the index
		if (!FRU_IS_ATYPE_VALID(atype) || atype != (uint8_t)FRU_AREA_NOT_PRESENT && atype != i) {
			errno = EINVAL;
			return NULL;
		}

		int area_offset_index = area_offsets[atype];
		uint8_t *offset = (uint8_t *)&fruhdr + area_offset_index;

		if(!data ||                                // No data is provided or
		   !FRU_AREA_HAS_SIZE(atype) && !blocks || // no size is given for a non-sized area or
		   !((fru_info_area_t *)data)->blocks     // the sized area contains a zero size
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

	// Calcute header checksum
	fruhdr.hchecksum = calc_checksum(&fruhdr, sizeof(fruhdr));
	out = calloc(1, FRU_BYTES(totalblocks));

	DEBUG("alocated a buffer at %p\n", out);
	if (!out) return NULL;

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

fru_t *find_fru_header(uint8_t *buffer, size_t size) {
	if (size < 8) {
		errno = ENOBUFS;
		return NULL;
	}
	fru_t *header = (fru_t *) buffer;
	if ((header->ver != FRU_VER_1) || (header->rsvd != 0) || (header->pad != 0)) {
		errno = EPROTO;
		return NULL;
	}
	if (header->hchecksum != calc_checksum(header, sizeof(fru_t) - 1)) {
		errno = EPROTO;
		return NULL;
	}
	return header;
}

#define AREA(NAME)                                                                 \
fru_##NAME##_area_t *find_fru_##NAME##_area(uint8_t *buffer, size_t size) {        \
	fru_t *header = find_fru_header(buffer, size);                             \
	if ((header == NULL) || (header->NAME == 0)) {                             \
		return NULL;                                                       \
	}                                                                          \
	if ((header->NAME + 3) > size) {                                           \
		errno = ENOBUFS;                                                   \
		return NULL;                                                       \
	}                                                                          \
	fru_##NAME##_area_t *area =                                                \
	    (fru_##NAME##_area_t *)(buffer + FRU_BYTES(header->NAME));             \
	if (area->ver != 1) {                                                      \
		errno = EPROTO;                                                    \
		return NULL;                                                       \
	}                                                                          \
	if (FRU_BYTES(header->NAME) + FRU_BYTES(area->blocks) > size) {            \
		errno = ENOBUFS;                                                   \
		return NULL;                                                       \
	}                                                                          \
	if (*(((uint8_t *)area) + FRU_BYTES(area->blocks) - 1) !=                  \
	    calc_checksum(((uint8_t *)area), FRU_BYTES(area->blocks) - 1)) {       \
		errno = EPROTO;                                                    \
		return NULL;                                                       \
	}                                                                          \
	return area;                                                               \
}
AREA(chassis);
AREA(board);
AREA(product);

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

		if (!fru_decode_data(field->&typelen, &field->data,,
				     FRU_FIELDMAXARRAY)) {
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

next:
		free((void *)field);
		printf("\n\n");
	}
}

int main(int argc, char *argv[])
{
	test_encodings();
	exit(1);
}
#endif
