#ifndef __GDBUS_CHECKSUM_H__
#define __GDBUS_CHECKSUM_H__

#include <stdint.h>

typedef enum
{
	CSM_NONE  = 0x00,
	CSM_CRC32 = 0x20,
	CSM_CRC32_0001 = 0xFF, // use crc32_0001() and watch for x64 bug
} checksum_mode_enum;

extern void        init_crc32(uint32_t* seed);
extern uint32_t    crc32(uint32_t seed, void* buf, int size);
extern void        update_checksum(unsigned char* checksum, char* buf, int size);

#endif /* CHECKSUM_H_ */
