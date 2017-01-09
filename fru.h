/** @file
 *  @brief Header for FRU information helper functions
 */

#ifndef __FRULIB_FRU_H__
#define __FRULIB_FRU_H__

#include <stdint.h>
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
	FRU_CHASSIS_TYPE,
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

/* FRU Area container structure for internal library use only */
typedef struct fru_area_s {
	uint8_t atype;
	uint8_t blocks;
	void * data;
} fru_area_t;

typedef struct fru_field_s {
	uint8_t typelen;   /**< Type/length of the field */
	uint8_t data[];    /**< The field data */
} fru_field_t;

typedef struct fru_reclist_s {
	fru_field_t *rec;
	struct fru_reclist_s *next;
} fru_reclist_t;

#define FRU_VER_1    1
#define FRU_MINIMUM_AREA_HEADER \
	uint8_t ver:4, rsvd:4;  /**< Area format version */

#define FRU_GENERIC_AREA_HEADER \
	FRU_MINIMUM_AREA_HEADER; \
	uint8_t blocks            /**< Size in 8-byte blocks */


#define LANG_DEFAULT 0
#define LANG_ENGLISH 25
#define FRU_LANG_AREA_HEADER \
	FRU_GENERIC_AREA_HEADER; \
	uint8_t lang       /**< Area language code */

typedef struct fru_sized_area_s { // The generic area structure
	FRU_GENERIC_AREA_HEADER;
	uint8_t data[];
} fru_sized_area_t;

#define FRU_SIZED_AREA_HEADER_SZ sizeof(fru_sized_area_t)

typedef fru_sized_area_t fru_chassis_area_t; /* The chassis area doesn't have any fixed fields beyond generic header */

typedef struct fru_internal_use_area_s {
	FRU_MINIMUM_AREA_HEADER;
	uint8_t data[];
} fru_internal_use_area_t;

typedef struct fru_board_area_s {
	FRU_LANG_AREA_HEADER;
	uint8_t mfgdate[3]; ///< Manufacturing date/time in seconds since 1996/1/1 0:00
	uint8_t data[];     ///< Variable size (multiple of 8 bytes) data with tail padding and checksum
} fru_board_area_t;

typedef struct fru_product_area_s {
	FRU_LANG_AREA_HEADER;
	uint8_t data[];
} fru_product_area_t;

#define FRU_AREA_HAS_LANG(t) (FRU_BOARD_INFO == (t) || FRU_PRODUCT_INFO == (t))
#define FRU_LANG_AREA_HEADER_SZ sizeof(fru_product_area_t)

#define FRU_AREA_HAS_DATE(t) (FRU_BOARD_INFO == (t))
#define FRU_DATE_AREA_HEADER_SZ sizeof(fru_board_area_t)

#define FRU_AREA_HAS_SIZE(t) (FRU_INTERNAL_USE < (t) && (t) < FRU_MULTIRECORD)
#define FRU_HAS_HEADER(t)    (FRU_MULTIRECORD != (t))

#define __TYPE_BITS_SHIFT     6
#define __TYPE_BITS_MASK      0xC0
#define __TYPE_BINARY         0x00
#define __TYPE_BCDPLUS        0x01
#define __TYPE_ASCII_6BIT     0x02
#define __TYPE_TEXT           0x03

/** FRU field type. Any of BINARY, BCDPLUS, ASCII_6BIT or TEXT. */
#define FRU_MAKETYPE(x)        (__TYPE_##x << __TYPE_BITS_SHIFT)
#define FRU_FIELDDATALEN(x)   ((x) & ~__TYPE_BITS_MASK)
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

/** Copy a FRU area field to a buffer and return the field's size */
static inline uint8_t fru_field_copy(void *dest, const fru_field_t *fieldp)
{
	memcpy(dest, (void *)fieldp, FRU_FIELDSIZE(fieldp->typelen));
	return FRU_FIELDSIZE(fieldp->typelen);
}

uint8_t fru_get_typelen(int len, const uint8_t *data);
fru_field_t * fru_encode_data(int len, const uint8_t *data);
unsigned char * fru_decode_data(const fru_field_t *field);
fru_board_area_t * fru_board_info(uint8_t lang,
                                  const struct timeval *tv,
                                  const unsigned char *mfg,
                                  const unsigned char *pname,
                                  const unsigned char *serial,
                                  const unsigned char *pn,
                                  const unsigned char *file,
                                  fru_reclist_t *cust);
fru_product_area_t * fru_product_info(uint8_t lang,
                                      const unsigned char *mfg,
                                      const unsigned char *pname,
                                      const unsigned char *pn,
                                      const unsigned char *ver,
                                      const unsigned char *serial,
                                      const unsigned char *atag,
                                      const unsigned char *file,
                                      fru_reclist_t *cust);
fru_t * fru_create(fru_area_t area[FRU_MAX_AREAS], size_t *size);

#endif // __FRULIB_FRU_H__
