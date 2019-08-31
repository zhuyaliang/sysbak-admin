#ifndef CHECKSUM_H_
#define CHECKSUM_H_

#include <stdint.h>

typedef enum
{
    CSM_NONE  = 0x00,
    CSM_CRC32 = 0x20,
    CSM_CRC32_0001 = 0xFF, // use crc32_0001() and watch for x64 bug
} checksum_mode_enum;

void init_crc32(uint32_t* seed);
uint32_t crc32(uint32_t seed, void* buf, int size);

unsigned get_checksum_size(int checksum_mode, int debug);
const char *get_checksum_str(int checksum_mode);
void init_checksum(int checksum_mode, unsigned char* seed, int debug);
void update_checksum(unsigned char* checksum, char* buf, int size);

#endif /* CHECKSUM_H_ */
