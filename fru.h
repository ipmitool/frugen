/** @file
 *  @brief Header for FRU information helper functions
 */

#include <stdint.h>
#include <string.h>
#include <sys/time.h>

typedef enum fru_area_type_e {
	FRU_AREA_UNKNOWN = 0,
	FRU_CHASSIS_INFO = 1,
	FRU_BOARD_INFO = 2,
	FRU_PRODUCT_INFO = 3,
} fru_area_type_t;

typedef struct fru_field_s {
	uint8_t typelen;   /**< Type/length of the field */
	uint8_t data[];    /**< The field data */
} fru_field_t;

typedef struct fru_reclist_s {
	fru_field_t *rec;
	struct fru_reclist_s *next;
} fru_reclist_t;

#define FRU_VER_1    1
#define FRU_GENERIC_AREA_HEADER \
	uint8_t ver;       /**< Area format version */   \
	uint8_t blocks     /**< Size in 8-byte blocks */


typedef struct fru_area_s { // The generic area structure
	FRU_GENERIC_AREA_HEADER;
	uint8_t data[];
} fru_area_t;

#define FRU_GENERIC_AREA_HEADER_SZ sizeof(fru_area_t)

typedef fru_area_t fru_chassis_area_t; /* The chassis area doesn't have any fixed fields beyond generic header */

#define LANG_DEFAULT 0
#define LANG_ENGLISH 25
#define FRU_LANG_AREA_HEADER \
	FRU_GENERIC_AREA_HEADER; \
	uint8_t lang       /**< Area language code */

#define FRU_AREA_HAS_LANG(t) (FRU_BOARD_INFO == (t) || FRU_PRODUCT_INFO == (t))

typedef struct fru_product_area_s {
	FRU_LANG_AREA_HEADER;
	uint8_t data[];
} fru_product_area_t;

#define FRU_LANG_AREA_HEADER_SZ sizeof(fru_product_area_t)

typedef struct fru_board_area_s {
	FRU_LANG_AREA_HEADER;
	uint8_t mfgdate[3]; ///< Manufacturing date/time in seconds since 1996/1/1 0:00
	uint8_t data[];     ///< Variable size (multiple of 8 bytes) data with tail padding and checksum
} fru_board_area_t;

#define FRU_AREA_HAS_DATE(t) (FRU_BOARD_INFO == (t))
#define FRU_DATE_AREA_HEADER_SZ sizeof(fru_board_area_t)

#define __TYPE_BITS_SHIFT     6
#define __TYPE_BITS_MASK      0xC0
#define __TYPE_BINARY         0x00
#define __TYPE_BCDPLUS        0x01
#define __TYPE_ASCII_6BIT     0x02
#define __TYPE_TEXT           0x03

/** FRU field type. Any of BINARY, BCDPLUS, ASCII_6BIT or TEXT. */
#define FRU_MAKETYPE(x)        (__TYPE_##x << __TYPE_BITS_SHIFT)
#define FRU_FIELDSIZE(typelen) (FRU_FIELDDATALEN(typelen) + sizeof(fru_field_t))
#define FRU_FIELDDATALEN(x)   ((x) & ~__TYPE_BITS_MASK)
#define FRU_TYPELEN(t, l)     (FRU_MAKETYPE(t) | FRU_FIELDDATALEN(l))
#define FRU_TYPE(t)           (((t) & __TYPE_BITS_MASK) >> __TYPE_BITS_SHIFT)
#define FRU_ISTYPE(t, type)   (FRU_TYPE(t) == __TYPE_##type)

#define LEN_AUTO              0

#define FRU_6BIT_LENGTH(len)    (((len) * 3 + 3) / 4)
#define FRU_6BIT_FULLLENGTH(l6) (((l6) * 4) / 3)
#define FRU_FIELD_EMPTY       FRU_TYPELEN(TEXT, 0)
#define FRU_FIELD_TERMINATOR  FRU_TYPELEN(TEXT, 1)

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
