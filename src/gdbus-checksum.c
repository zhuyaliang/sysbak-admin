#include "gdbus-checksum.h"
#include <stdio.h>

#define CRC32_SEED 0xFFFFFFFF

static uint32_t crc_tab32[256] = { 0 };
static int      cs_mode = CSM_NONE;


void init_crc32(uint32_t* seed) 
{
	if (crc_tab32[0] == 0) 
	{
		/// initial crc table
		uint32_t init_crc, init_p;
		uint32_t i, j;
		init_p = 0xEDB88320L;

		for (i = 0; i < 256; i++) 
		{
			init_crc = i;
			for (j = 0; j < 8; j++) 
			{
				if (init_crc & 0x00000001L)
					init_crc = ( init_crc >> 1 ) ^ init_p;
				else
					init_crc = init_crc >> 1;
			}

			crc_tab32[i] = init_crc;
		}
	}

	*seed = CRC32_SEED;
}

uint32_t crc32(uint32_t seed, void* buffer, int size) 
{
	unsigned char * buf = (unsigned char *)buffer;
	const unsigned char * end = buf + size;
	uint32_t tmp, long_c, crc = seed;
	while (buf != end) 
	{
		/// update crc
		long_c = *(buf++);
		tmp = crc ^ long_c;
		crc = (crc >> 8) ^ crc_tab32[tmp & 0xff];
	};

	return crc;
}

static uint32_t crc32_0001(uint32_t seed, void* buffer, int size) {

	unsigned char * buf = (unsigned char *)buffer;
	uint32_t tmp, long_c, crc = seed;
	int s = 0;

	while (s++ < size) {
		/// update crc
		long_c = *buf; // note: yes, 0001 is missing a "++" here
		tmp = crc ^ long_c;
		crc = (crc >> 8) ^ crc_tab32[ tmp & 0xff ];
	};

	return crc;
}

void update_checksum(unsigned char* checksum, char* buf, int size) {

	uint32_t* crc;

	switch(cs_mode)
	{
	case CSM_CRC32:
		crc = (uint32_t*)checksum;
		*crc = crc32(*crc, (unsigned char*)buf, size);
		break;

	case CSM_CRC32_0001:
		crc = (uint32_t*)checksum;
		*crc = crc32_0001(*crc, (unsigned char*)buf, size);
		break;

	case CSM_NONE:
		// Nothing to do
		// Leave checksum alone as it may be NULL or point to a zero-sized array.
		break;
    default:
        break;
	}

}
