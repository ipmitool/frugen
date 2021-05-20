/** @file
 *  @brief Header for FRU information helper functions
 *
 *  Copyright (C) 2016-2021 Alexander Amelkin <alexander@amelkin.msk.ru>
 *  SPDX-License-Identifier: LGPL-2.0-or-later OR Apache-2.0
 */

#ifndef __FRULIB_FRU_H__
#define __FRULIB_FRU_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define ARRAY_SZ(a) (sizeof(a) / sizeof((a)[0]))

typedef struct fru_s {
	uint8_t ver:4, rsvd:4;
	uint8_t internal;
	uint8_t chassis;
	uint8_t board;
	uint8_t product;
	uint8_t multirec;
	uint8_t pad;
	uint8_t hchecksum; ///< Header checksum
	uint8_t data[];
} fru_t;

typedef enum fru_area_type_e {
	FRU_AREA_NOT_PRESENT = -1,
	FRU_INTERNAL_USE,
	FRU_CHASSIS_INFO,
	FRU_BOARD_INFO,
	FRU_PRODUCT_INFO,
	FRU_MULTIRECORD,
	FRU_MAX_AREAS
} fru_area_type_t;

typedef enum {
	FRU_CHASSIS_PARTNO,
	FRU_CHASSIS_SERIAL
} fru_chassis_field_t;

typedef enum {
	FRU_BOARD_MFG,
	FRU_BOARD_PRODNAME,
	FRU_BOARD_SERIAL,
	FRU_BOARD_PARTNO,
	FRU_BOARD_FILE
} fru_board_field_t;

typedef enum {
	FRU_PROD_MFG,
	FRU_PROD_NAME,
	FRU_PROD_MODELPN,
	FRU_PROD_VERSION,
	FRU_PROD_SERIAL,
	FRU_PROD_ASSET,
	FRU_PROD_FILE
} fru_prod_field_t;

/// Table 16-2, MultiRecord Area Record Types
typedef enum {
	FRU_MR_MIN = 0x00,
	FRU_MR_PSU_INFO = 0x00,
	FRU_MR_DC_OUT = 0x01,
	FRU_MR_DC_LOAD = 0x02,
	FRU_MR_MGMT_ACCESS = 0x03,
	FRU_MR_BASE_COMPAT = 0x04,
	FRU_MR_EXT_COMPAT = 0x05,
	FRU_MR_ASF_FIXED_SMBUS = 0x06,
	FRU_MR_ASF_LEGACY_ALERTS = 0x07,
	FRU_MR_ASF_REMOTE_CTRL = 0x08,
	FRU_MR_EXT_DC_OUT = 0x09,
	FRU_MR_EXT_DC_LOAD = 0x0A,
	FRU_MR_NVME_B = 0x0B,
	FRU_MR_NVME_C = 0x0C,
	FRU_MR_NVME_D = 0x0D,
	FRU_MR_NVME_E = 0x0E,
	FRU_MR_NVME_F = 0x0F,
	FRU_MR_OEM_START = 0xC0,
	FRU_MR_OEM_END = 0xFF,
	FRU_MR_MAX = 0xFF
} fru_mr_type_t;

/// Table 18-6, Management Access Record
typedef enum {
	FRU_MR_MGTM_MIN = 0x01,
	FRU_MR_MGMT_SYS_URL = 0x01,
	FRU_MR_MGMT_SYS_NAME = 0x02,
	FRU_MR_MGMT_SYS_PING = 0x03,
	FRU_MR_MGMT_COMPONENT_URL = 0x04,
	FRU_MR_MGMT_COMPONENT_NAME = 0x05,
	FRU_MR_MGMT_COMPONENT_PING = 0x06,
	FRU_MR_MGMT_SYS_UUID = 0x07,
	FRU_MR_MGTM_MAX = 0x07,
} fru_mr_mgmt_type_t;

#define FRU_IS_ATYPE_VALID(t) ((t) >= FRU_AREA_NOT_PRESENT && (t) < FRU_MAX_AREAS)

/**
 * FRU area description structure.
 *
 * Contains information about a single arbitrary area.
 */
typedef struct fru_area_s {
	fru_area_type_t atype; /**< FRU area type */
	uint8_t blocks; /**< Size of the data field in 8-byte blocks */
	void * data; /**< Pointer to the actual FRU area data */
} fru_area_t;

/**
 * Generic FRU area field header structure.
 *
 * Every field in chassis, board and product information areas has such a header.
 */
typedef struct fru_field_s {
	uint8_t typelen;   /**< Type/length of the field */
	uint8_t data[];    /**< The field data */
} fru_field_t;


/**
 * A single-linked list of FRU area fields.
 *
 * This is used to describe any length chains of fields.
 * Mandatory fields are linked first in the order they appear
 * in the information area (as per Specification), then custom
 * fields are linked.
 */
typedef struct fru_reclist_s {
	fru_field_t *rec; /**< A pointer to the field or NULL if not initialized */
	struct fru_reclist_s *next; /**< The next record in the list or NULL if last */
} fru_reclist_t;

/**
 * Allocate a new reclist entry and add it to \a reclist,
 * set \a reclist to point to the newly allocated entry if
 * \a reclist was NULL.
 *
 * @returns Pointer to the added entry
 */
static inline fru_reclist_t *add_reclist(fru_reclist_t **reclist)
{
	fru_reclist_t *rec;
	fru_reclist_t *reclist_ptr = *reclist;
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

/// Works both for fru_reclist_t* and for fru_mr_reclist_t*
#define free_reclist(recp) while(recp) { \
	typeof(recp->next) next = recp->next; \
	free(recp->rec); \
	free(recp); \
	recp = next; \
}

#define FRU_VER_1    1
#define FRU_MINIMUM_AREA_HEADER \
	uint8_t ver;  /**< Area format version, only lower 4 bits */

#define FRU_INFO_AREA_HEADER \
	FRU_MINIMUM_AREA_HEADER; \
	uint8_t blocks;         /**< Size in 8-byte blocks */ \
	uint8_t langtype        /**< Area language code or chassis type (from smbios.h) */

#define LANG_DEFAULT 0
#define LANG_ENGLISH 25

typedef struct fru_info_area_s { // The generic info area structure
	FRU_INFO_AREA_HEADER;
	uint8_t data[];
} fru_info_area_t;

#define FRU_INFO_AREA_HEADER_SZ sizeof(fru_info_area_t)

typedef struct fru_internal_use_area_s {
	FRU_MINIMUM_AREA_HEADER;
	uint8_t data[];
} fru_internal_use_area_t;

typedef fru_info_area_t fru_chassis_area_t;

typedef struct fru_board_area_s {
	FRU_INFO_AREA_HEADER;
	uint8_t mfgdate[3]; ///< Manufacturing date/time in seconds since 1996/1/1 0:00
	uint8_t data[];     ///< Variable size (multiple of 8 bytes) data with tail padding and checksum
} fru_board_area_t;

typedef fru_info_area_t fru_product_area_t;

typedef struct {
	uint8_t type_id; ///< Record Type ID
	uint8_t eol_ver;
#define FRU_MR_EOL 0x80 // End-of-list flag
#define FRU_MR_VER_MASK 0x07
#define FRU_MR_VER 0x02 // Version is always 2
	uint8_t len;           ///< Length of the raw `data` array
	uint8_t rec_checksum;
	uint8_t hdr_checksum;
} __attribute__((packed)) fru_mr_header_t;

typedef struct {
	fru_mr_header_t hdr;
	uint8_t data[];        ///< Raw data of size `len`
} __attribute__((packed)) fru_mr_rec_t;

typedef struct {
	fru_mr_header_t hdr;
	uint8_t subtype;
	uint8_t data[];
} __attribute__((packed)) fru_mr_mgmt_rec_t;

typedef struct fru_mr_reclist_s {
	fru_mr_rec_t *rec;
	struct fru_mr_reclist_s *next;
} fru_mr_reclist_t;

typedef fru_mr_rec_t fru_mr_area_t; /// Intended for use as a pointer only

#define FRU_AREA_HAS_DATE(t) (FRU_BOARD_INFO == (t))
#define FRU_DATE_AREA_HEADER_SZ sizeof(fru_board_area_t)
#define FRU_DATE_UNSPECIFIED 0

#define FRU_AREA_HAS_SIZE(t) (FRU_INTERNAL_USE < (t) && (t) < FRU_MULTIRECORD)
#define FRU_AREA_HAS_HEADER(t) (FRU_MULTIRECORD != (t))
#define FRU_AREA_IS_GENERIC(t) FRU_AREA_HAS_SIZE(t)

#define __TYPE_BITS_SHIFT     6
#define __TYPE_BITS_MASK      0xC0
#define __TYPE_BINARY         0x00
#define __TYPE_BCDPLUS        0x01
#define __TYPE_ASCII_6BIT     0x02
#define __TYPE_TEXT           0x03

/** FRU field type. Any of BINARY, BCDPLUS, ASCII_6BIT or TEXT. */
#define FRU_MAKETYPE(x)        (__TYPE_##x << __TYPE_BITS_SHIFT)
#define FRU_FIELDDATALEN(x)   ((x) & ~__TYPE_BITS_MASK)
#define FRU_FIELDMAXLEN       FRU_FIELDDATALEN(UINT8_MAX) // For FRU fields
#define FRU_FIELDMAXARRAY     (FRU_FIELDMAXLEN + 1) // For C array allocation
#define FRU_FIELDSIZE(typelen) (FRU_FIELDDATALEN(typelen) + sizeof(fru_field_t))
#define FRU_TYPELEN(t, l)     (FRU_MAKETYPE(t) | FRU_FIELDDATALEN(l))
#define FRU_TYPE(t)           (((t) & __TYPE_BITS_MASK) >> __TYPE_BITS_SHIFT)
#define FRU_ISTYPE(t, type)   (FRU_TYPE(t) == __TYPE_##type)

#define LEN_AUTO              0

#define FRU_6BIT_LENGTH(len)    (((len) * 3 + 3) / 4)
#define FRU_6BIT_FULLLENGTH(l6) (((l6) * 4) / 3)
#define FRU_FIELD_EMPTY       FRU_TYPELEN(TEXT, 0)
#define FRU_FIELD_TERMINATOR  FRU_TYPELEN(TEXT, 1)

#define FRU_BLOCK_SZ 8
#define FRU_BYTES(blocks) ((blocks) * FRU_BLOCK_SZ)
#define FRU_BLOCKS(bytes)  (((bytes) + FRU_BLOCK_SZ - 1) / FRU_BLOCK_SZ)

typedef struct {
	uint8_t type;
	unsigned char pn[FRU_FIELDMAXARRAY];
	unsigned char serial[FRU_FIELDMAXARRAY];
	fru_reclist_t *cust;
} fru_exploded_chassis_t;

typedef struct {
	uint8_t lang;
	struct timeval tv;
	unsigned char mfg[FRU_FIELDMAXARRAY];
	unsigned char pname[FRU_FIELDMAXARRAY];
	unsigned char serial[FRU_FIELDMAXARRAY];
	unsigned char pn[FRU_FIELDMAXARRAY];
	unsigned char file[FRU_FIELDMAXARRAY];
	fru_reclist_t *cust;
} fru_exploded_board_t;

typedef struct {
	uint8_t lang;
	unsigned char mfg[FRU_FIELDMAXARRAY];
	unsigned char pname[FRU_FIELDMAXARRAY];
	unsigned char pn[FRU_FIELDMAXARRAY];
	unsigned char ver[FRU_FIELDMAXARRAY];
	unsigned char serial[FRU_FIELDMAXARRAY];
	unsigned char atag[FRU_FIELDMAXARRAY];
	unsigned char file[FRU_FIELDMAXARRAY];
	fru_reclist_t *cust;
} fru_exploded_product_t;

#define fru_loadfield(eafield, value) strncpy(eafield, value, FRU_FIELDMAXLEN)

void fru_set_autodetect(bool enable);

fru_chassis_area_t * fru_chassis_info(const fru_exploded_chassis_t *chassis);
fru_board_area_t * fru_board_info(const fru_exploded_board_t *board);
fru_product_area_t * fru_product_info(const fru_exploded_product_t *product);

int fru_mr_uuid2rec(fru_mr_rec_t **rec, const unsigned char *str);
fru_mr_reclist_t * add_mr_reclist(fru_mr_reclist_t **reclist);
fru_mr_area_t * fru_mr_area(fru_mr_reclist_t *reclist, size_t *total);

fru_field_t * fru_encode_data(int len, const uint8_t *data);
fru_t * fru_create(fru_area_t area[FRU_MAX_AREAS], size_t *size);

#endif // __FRULIB_FRU_H__
