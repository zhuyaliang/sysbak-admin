/*  sysbak-admin 
*   Copyright (C) 2019  zhuyaliang https://github.com/zhuyaliang/
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.

*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.

*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef __SYSBAK_SHARE_H__
#define __SYSBAK_SHARE_H__


#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#define     DEFAULT_BUFFER_SIZE 1048576
#define CRC32_SIZE 4
#define IMAGE_MAGIC "partclone-image"
#define IMAGE_MAGIC_SIZE 15
#define IMAGE_VERSION_SIZE 4
#define FS_MAGIC_SIZE 15
#define PARTCLONE_VERSION_SIZE (FS_MAGIC_SIZE-1)
#define IMAGE_VERSION_0002 "0002"
#define VERSION "0.3.13"
#define     BACK_DD   1    //dd if 
#define	    BACK_PTF  2        
#define	    BACK_PTP  3
#define     RESTORE   4
#define ENDIAN_MAGIC 0xC0DE
#define WRITE         1 
#define READ          0 

typedef struct
{
    // Size of the source device, in bytes
	unsigned long long device_size;

	// Number of blocks in the file system
	unsigned long long totalblock;

	// Number of blocks in use as reported by the file system
	unsigned long long usedblocks;

	// Number of blocks in use in the bitmap
	unsigned long long used_bitmap;

	// Number of bytes in each block
	unsigned int  block_size;

	// checksum algorithm used (see checksum_mode_enum)
	uint16_t checksum_mode;

	// Size of one checksum, in bytes. 0 when NONE, 4 with CRC32, etc.
	uint16_t checksum_size;

	// How many consecutive blocks are checksumed together.
	uint32_t blocks_per_checksum;
}file_system_info;

typedef struct
{
    char magic[IMAGE_MAGIC_SIZE+1];

    /// Partclone's version who created the image, ex: "2.61"
    char ptc_version[PARTCLONE_VERSION_SIZE];

    /// Image's version
    char version[IMAGE_VERSION_SIZE];

    /// 0xC0DE = little-endian, 0xDEC0 = big-endian
    uint16_t endianess;

}image_head;

typedef struct
{
	float  percent;
    float  speed;
    time_t remained;
    time_t elapsed;
}progress_data;
typedef struct
{
	image_head       head;
	file_system_info fs_info;
	uint32_t            crc;
}image_desc;

typedef unsigned long long ull;
typedef unsigned int       uint;
typedef void (* sysbak_progress) (progress_data*,gpointer);

int         open_source_device             (const char       *device, 
		                                    int               mode);

int         open_target_device             (const char       *target, 
		                                    int               mode,
											gboolean          overwrite); 

gboolean    check_memory_size              (file_system_info  fs_info,
		                                    const uint32_t    blkcs);

void        update_used_blocks_count       (file_system_info *fs_info, 
		                                    unsigned long     *bitmap); 

ull         convert_blocks_to_bytes        (ull               block_offset, 
                                            unsigned int      block_count, 
                                            unsigned int      block_sizei,
										    unsigned int      blocks_per_checksum,
										    unsigned int      checksum_size); 

gboolean    write_image_bitmap             (int              *fd, 
                                            file_system_info  fs_info, 
                                            unsigned long    *bitmap); 

ull         get_local_free_space           (const char       *path);

ull         get_partition_free_space       (int              *fd); 

int         write_read_io_all              (int              *fd, 
		                                    char             *buf, 
											ull               count, 
											int               do_write);

void        init_file_system_info          (file_system_info *fs_info);

gboolean    write_image_desc               (int              *fd, 
		                                    file_system_info  fs_info); 

void        print_file_system_info         (file_system_info  fs_info);
#endif
