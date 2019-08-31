#include "checksum.h"

#define CRC32_SEED 0xFFFFFFFF

static uint32_t crc_tab32[256] = { 0 };
static int cs_mode = CSM_NONE;

unsigned get_checksum_size(int checksum_mode, int debug) {

	switch(checksum_mode) {

	case CSM_NONE:
		return 0;

	case CSM_CRC32:
	case CSM_CRC32_0001:
		return 4;

	default:
		return UINT_LEAST32_MAX;
		break;
	}
}

const char *get_checksum_str(int checksum_mode) {

	switch(checksum_mode) {

	case CSM_NONE:
		return "NONE";

	case CSM_CRC32:
		return "CRC32";

	case CSM_CRC32_0001:
		return "CRC32_0001";

	default:
		return "UNKNOWN";
	}
}

/**
 * Initialise crc32 lookup table if it is not already done and initialise seed
 * the the default implementation seed value
 */
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

/**
 * Initiate the checksum library and seed based on checksum_mode.
 *
 * The main goal if this function is keep the code independent of the algorithm used.
 * To accomplish this, the caller is responsible to allocate enough room to store the
 * checksum.
 */
void init_checksum(int checksum_mode, unsigned char* seed)
{

	switch(checksum_mode) {

	case CSM_CRC32:
		init_crc32((uint32_t*)seed);
		break;

	case CSM_CRC32_0001:
		init_crc32((uint32_t*)seed);
		break;

	case CSM_NONE:
		// Nothing to do
		// Leave seed alone as it may be NULL or point to a zero-sized array
		break;

	default:
		break;
	}

	cs_mode = checksum_mode;
}

/// the crc32 function, reference from libcrc.
/// Author is Lammert Bies  1999-2007
/// Mail: info@lammertbies.nl
/// http://www.lammertbies.nl/comm/info/nl_crc-calculation.html
/// generate crc32 code
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

/**
 * Note
 * This version is only used to detect the x64 bug existing in old image version 0001.
 * Once we know if the image as the bug or not, we disable the checksum check. The x64
 * bug caused the crc32 to be recorded with 8 bytes instead of 4. So the data stream
 * is like this: <block><crc><bug><block><crc><bug>...
 */
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

/**
 * Update the checksum by using the algorithm set when init_checksum() was call.
 *
 * The main goal if this function is keep the code independent of the algorithm used.
 * To accomplish this, the caller is responsible to allocate enough room to store the
 * checksum.
 */
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
	}

}
