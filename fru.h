/** @file
 *  @brief Header for FRU information helper functions
 */

#ifndef __FRULIB_FRU_H__
#define __FRULIB_FRU_H__

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
	rec = malloc(sizeof(*rec));
	if(!rec) return NULL;
	memset(rec, 0, sizeof(*rec));

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
#define free_reclist(recp) while(recp) { fru_reclist_t *next = recp->next; free(recp); recp = next; }

#define FRU_VER_1    1
#define FRU_MINIMUM_AREA_HEADER \
	uint8_t ver:4, rsvd:4;  /**< Area format version */

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
#define FRU_FIELDMAXLEN       FRU_FIELDDATALEN(UINT8_MAX)
#define FRU_FIELDMAXSTRLEN    (FRU_FIELDDATALEN(UINT8_MAX) + 1)
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
	unsigned char pn[FRU_FIELDMAXSTRLEN];
	unsigned char serial[FRU_FIELDMAXSTRLEN];
	fru_reclist_t *cust;
} fru_exploded_chassis_t;

typedef struct {
	uint8_t lang;
	struct timeval tv;
	unsigned char mfg[FRU_FIELDMAXSTRLEN];
	unsigned char pname[FRU_FIELDMAXSTRLEN];
	unsigned char serial[FRU_FIELDMAXSTRLEN];
	unsigned char pn[FRU_FIELDMAXSTRLEN];
	unsigned char file[FRU_FIELDMAXSTRLEN];
	fru_reclist_t *cust;
} fru_exploded_board_t;

typedef struct {
	uint8_t lang;
	unsigned char mfg[FRU_FIELDMAXSTRLEN];
	unsigned char pname[FRU_FIELDMAXSTRLEN];
	unsigned char pn[FRU_FIELDMAXSTRLEN];
	unsigned char ver[FRU_FIELDMAXSTRLEN];
	unsigned char serial[FRU_FIELDMAXSTRLEN];
	unsigned char atag[FRU_FIELDMAXSTRLEN];
	unsigned char file[FRU_FIELDMAXSTRLEN];
	fru_reclist_t *cust;
} fru_exploded_product_t;

#define fru_loadfield(eafield, value) strncpy(eafield, value, FRU_FIELDMAXLEN)

fru_chassis_area_t * fru_chassis_info(const fru_exploded_chassis_t *chassis);
fru_board_area_t * fru_board_info(const fru_exploded_board_t *board);
fru_product_area_t * fru_product_info(const fru_exploded_product_t *product);
fru_field_t * fru_encode_data(int len, const uint8_t *data);
fru_t * fru_create(fru_area_t area[FRU_MAX_AREAS], size_t *size);

#endif // __FRULIB_FRU_H__
