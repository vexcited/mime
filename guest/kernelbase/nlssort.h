#ifndef MIME_NLSSORT_H
#define MIME_NLSSORT_H

#include <stddef.h>
#include <stdint.h>

typedef struct NLS_SORT_ORDER {
    uint32_t Flags;
    const uint16_t *PageMap;
    const uint16_t *LingPageMap;
    uint16_t Min;
    uint16_t Max;
    uint16_t Count[9];
    const uint16_t *Entries[9];
} NLS_SORT_ORDER;

#define NLS_SORT_THREE_BYTE_WEIGHTS 0x01
#define NLS_SORT_REVERSE_DIACRITICS 0x10
#define NLS_SORT_DOUBLE_COMPRESSION 0x20

typedef struct NLS_SORT_TABLES {
    const uint32_t *Pages;
    const uint32_t *Expansions;
    const uint8_t *Jamo;
} NLS_SORT_TABLES;

#define NLS_SORT_IGNORECASE 0x00000001
#define NLS_SORT_IGNORENONSPACE 0x00000002
#define NLS_SORT_IGNORESYMBOLS 0x00000004
#define NLS_SORT_DIGITSASNUMBERS 0x00000008
#define NLS_SORT_LINGUISTIC_IGNORECASE 0x00000010
#define NLS_SORT_LINGUISTIC_IGNOREDIACRITIC 0x00000020
#define NLS_SORT_STRINGSORT 0x00001000
#define NLS_SORT_IGNOREKANATYPE 0x00010000
#define NLS_SORT_IGNOREWIDTH 0x00020000
#define NLS_SORT_LINGUISTIC_CASING 0x08000000
/* the weights as the comparison takes them, which is not what the sort key holds for the marks. */
#define NLS_SORT_COMPARE 0x80000000u
#define NLS_SORT_FIND_STARTSWITH 0x00100000
#define NLS_SORT_FIND_ENDSWITH 0x00200000
#define NLS_SORT_FIND_FROMSTART 0x00400000
#define NLS_SORT_FIND_FROMEND 0x00800000

/* what the engine gets its memory from for the long strings, which the user of it provides. */
void *NlsSortAlloc(size_t size);
void NlsSortFree(void *p);

/* the sort key of a string; a size of 0 gives the size the key needs, and -1 is returned when the buffer is too small. */
int NlsSortKey(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *text, int length,
               uint8_t *key, int size);

/* the order of two strings as CompareString gives it: 1 for the first one less, 2 for equal and 3 for greater, 0 when memory is short. */
int NlsSortCompare(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *a, int alen,
                   const uint16_t *b, int blen);

/* the place of a string in another with the flags of FindNLSString: its start, and its length in the other, or -1. */
int NlsSortFind(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *text, int length,
                const uint16_t *value, int value_length, int *found_length);

/* whether the characters of a string are defined in the order of Windows, as IsNLSDefinedString says. */
int NlsSortIsDefined(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, const uint16_t *text, int length);

/* the hash of the weights of a string, the one LCMAP_HASH gives. */
uint32_t NlsSortHash(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *text, int length);

#endif
