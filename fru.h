/** @file
 *  @brief Header for FRU information helper functions
 */

#include <stdint.h>
#include <string.h>
#include <sys/time.h>

typedef struct fru_field_s {
	uint8_t typelen;
	uint8_t data[];
} fru_field_t;

typedef struct fru_area_s {
#define FRU_VER_1    1
	uint8_t ver;        ///< Area format version
	uint8_t blocks;     ///< Size in 8-byte blocks
#define LANG_DEFAULT 0
#define LANG_ENGLISH 25
	uint8_t lang;       ///< Area language code
	uint8_t mfgdate[3]; ///< Manufacturing date/time in seconds since 1996/1/1 0:00
	uint8_t data[];     ///< Variable size (multiple of 8 bytes) data with tail padding and checksum
} fru_area_t;

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

static inline void * fru_fieldcopy(void *dest, const fru_field_t *fieldp)
{
	void *ret;
	ret = memcpy(dest, (void *)fieldp, FRU_FIELDSIZE(fieldp->typelen));
	dest += FRU_FIELDSIZE(fieldp->typelen);

	return ret;
}

uint8_t fru_get_typelen(int len, const uint8_t *data);
fru_field_t * fru_encode_data(int len, const uint8_t *data);
unsigned char * fru_decode_data(const fru_field_t *field);
fru_area_t * fru_board_info(int lang,
                            struct timeval tv,
                            const unsigned char *mfg,
                            const unsigned char *pname,
                            const unsigned char *serial,
                            const unsigned char *pn,
                            const unsigned char *file,
                            const fru_field_t **cust);

