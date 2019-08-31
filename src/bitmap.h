#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PART_BYTES_PER_LONG ((int)sizeof(unsigned long))
#define PART_BITS_PER_BYTE  (8)
#define PART_BITS_PER_LONG  (PART_BYTES_PER_LONG*8)
#define BITS_TO_BYTES(bits) (((bits)+PART_BITS_PER_BYTE-1)/PART_BITS_PER_BYTE)
#define BITS_TO_LONGS(bits) (((bits)+PART_BITS_PER_LONG-1)/PART_BITS_PER_LONG)

static inline int
pc_test_bit(unsigned long int nr, unsigned long *bitmap,
	    unsigned long long total)
{
	if (!bitmap)
		return -1;
	if (nr >= total){
	    printf("test block %lu out of boundary(%llu)\n", nr, total);
		exit(1);
	}
	unsigned long offset = nr / PART_BITS_PER_LONG;
	unsigned long bit = nr & (PART_BITS_PER_LONG - 1);
	return (bitmap[offset] >> bit) & 1;
}

static inline void
pc_set_bit(unsigned long int nr, unsigned long *bitmap,
	   unsigned long long total)
{
	if (!bitmap)
		return;
	if (nr >= total){
	    printf("set block %lu out of boundary(%llu)\n", nr, total);
		exit(1);
	}
	unsigned long offset = nr / PART_BITS_PER_LONG;
	unsigned long bit = nr & (PART_BITS_PER_LONG - 1);
	bitmap[offset] |= 1UL << bit;
}

static inline void
pc_clear_bit(unsigned long int nr, unsigned long *bitmap,
	     unsigned long long total)
{
	if (!bitmap)
		return;
	if (nr >= total){
	    printf("clear block %lu out of boundary(%llu)\n", nr, total);
		exit(1);
	}
	unsigned long offset = nr / PART_BITS_PER_LONG;
	unsigned long bit = nr & (PART_BITS_PER_LONG - 1);
	bitmap[offset] &= ~(1UL << bit);
}

static inline unsigned long* pc_alloc_bitmap(unsigned long bits)
{
	return (unsigned long*)calloc(PART_BYTES_PER_LONG, BITS_TO_LONGS(bits));
}

static inline void pc_init_bitmap(unsigned long* bitmap, char value, unsigned long bits)
{
	unsigned long byte_count = PART_BYTES_PER_LONG * BITS_TO_LONGS(bits);

	memset(bitmap, value, byte_count);
}
