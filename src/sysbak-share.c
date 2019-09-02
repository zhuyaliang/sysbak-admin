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
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <features.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include <mntent.h>
#include <errno.h>
#include <stdio.h>
#include "sysbak-share.h"
#include "checksum.h"
#include "bitmap.h"
#if defined(linux) && defined(_IO) && !defined(BLKGETSIZE)
#define BLKGETSIZE      _IO(0x12,96)  /* Get device size in 512-byte blocks. */
#endif
#if defined(linux) && defined(_IOR) && !defined(BLKGETSIZE64)
#define BLKGETSIZE64    _IOR(0x12,114,size_t)   /* Get device size in bytes. */
#endif
/// the io function, reference from ntfsprogs(ntfsclone).
int write_read_io_all(int *fd, char *buf, ull count, int do_write) 
{
	long long int i;
	unsigned long long size = count;
	// for sync I/O buffer, when use stdin or pipe.
	while (count > 0) 
    {
		if (do_write) 
        {
			i = write(*fd, buf, count);
        } 
        else 
        {
			i = read(*fd, buf, count);
        }
		if (i < 0) 
        {
			if (errno != EAGAIN && errno != EINTR) 
            {
				return -1;
			}
		} 
        else if (i == 0) 
        {
			return 0;
		} 
        else 
        {
			count -= i;
			buf = i + (char *) buf;
		}
	}
	return size;
}
static int is_block_type (const char *device)
{
    struct stat st_dev;
    int ddd_block_device;

    if (stat(device, &st_dev) != -1) 
    {
        if (S_ISBLK(st_dev.st_mode)) 
		    ddd_block_device = 1;
		else
		    ddd_block_device = 0;
	}
    else
    {
		ddd_block_device = 0;   
	}
    
    return ddd_block_device;
}    

static gboolean check_mount(const char* device)
{
	char *real_file = NULL, *real_fsname = NULL;
	FILE * f;
	struct mntent * mnt;
	gboolean isMounted = FALSE;

	real_file = malloc(PATH_MAX + 1);
	if (!real_file) 
    {
		goto EXIT;
	}
	real_fsname = malloc(PATH_MAX + 1);
	if (!real_fsname) 
    {
	    goto EXIT;
    }

	if (!realpath(device, real_file)) 
    {
	    goto EXIT;
	}
	if ((f = setmntent(MOUNTED, "r")) == 0) 
    {
	    goto EXIT;
	}
	while ((mnt = getmntent (f)) != 0) 
    {
		if (!realpath(mnt->mnt_fsname, real_fsname))
			continue;
		if (strcmp(real_file, real_fsname) == 0) 
        {
			isMounted = 1;
		}
	}
	endmntent(f);
EXIT:
	if (real_file)
    { 
        free(real_file); 
        real_file = NULL;
    }
	if (real_fsname)
    { 
        free(real_fsname); 
        real_fsname = NULL;
    }
	return isMounted;
}

int open_source_device(const char *device,int mode) 
{
	int fd = 0;
	int flags = O_RDONLY | O_LARGEFILE;
	int ddd_block_device = -1;

	if (mode == BACK_DD) 
    {
	    ddd_block_device = is_block_type (device);
	}
	if (mode == BACK_PTF || mode == BACK_PTP || ddd_block_device == 1) 
    { 
		if (check_mount(device)) 
        {
			return 0;
		}
		fd = open(device, flags, S_IRUSR);
	} 
    //dd if or restore
    else if (mode == RESTORE || ddd_block_device == 0) 
    {
	    fd = open (device, flags, S_IRWXU);
	}

	return fd;
}

int open_target_device(const char* target, int mode,gboolean overwrite) 
{
	int ret = 0;
    struct stat st_dev;
	int flags = O_WRONLY | O_LARGEFILE;
	int ddd_block_device = -1;

	if (mode == BACK_DD) 
    {
	    ddd_block_device = is_block_type (target);
	}

	if (mode == BACK_PTF || ddd_block_device == 0)  //back-up parct to file
    {
		flags |= O_CREAT | O_TRUNC;
		if (!overwrite)
			flags |= O_EXCL;
		ret = open(target, flags, S_IRUSR|S_IWUSR);
	}
    // back-up parct to parct or restore or dd to block
    else if (mode == RESTORE || mode == BACK_PTP || (ddd_block_device == 1)) 
    {    
		if (check_mount(target)) 
        {
		    return -1;
        }
		stat(target, &st_dev);
		if (!S_ISBLK(st_dev.st_mode)) 
        {
			flags |= O_CREAT;
			if (!overwrite)
				flags |= O_EXCL;
		}
		ret = open (target, flags, S_IRUSR);
	} 
	return ret;
}

gboolean check_memory_size(file_system_info fs_info,image_options img_opt)
{
	const ull bitmap_size = BITS_TO_BYTES(fs_info.totalblock);
	const uint32_t blkcs = img_opt.blocks_per_checksum;
	const uint32_t block_size = fs_info.block_size;
	const unsigned int buffer_capacity = DEFAULT_BUFFER_SIZE > block_size ? DEFAULT_BUFFER_SIZE / block_size : 1;
	const ull raw_io_size = buffer_capacity * block_size;
    unsigned long long cs_size = 0;
	void *test_bitmap, *test_read, *test_write;
    unsigned long long cs_in_buffer = buffer_capacity / blkcs;
    
	cs_size = cs_in_buffer * img_opt.checksum_size;
	
	test_bitmap = malloc(bitmap_size);
	test_read   = malloc(raw_io_size);
	test_write  = malloc(raw_io_size + cs_size);

    free(test_bitmap);
    free(test_read);
    free(test_write);
	if (test_bitmap == NULL || test_read == NULL || test_write == NULL) 
    {
        return FALSE;
    }

    return TRUE;
}
static void set_image_options(image_options* img_opt)
{
    img_opt->feature_size = sizeof(image_options);
    img_opt->image_version = 0x0002;
    img_opt->checksum_mode = CSM_CRC32;
    img_opt->checksum_size = CRC32_SIZE;
    img_opt->blocks_per_checksum = 0;
    img_opt->reseed_checksum = 1;
    img_opt->bitmap_mode = BM_BIT;
}

void init_file_system_info(file_system_info *fs_info)
{
    memset(fs_info, 0, sizeof(file_system_info));
}
void init_image_options(image_options* img_opt)
{
	char *p;

    memset(img_opt, 0, sizeof(image_options));
    img_opt->cpu_bits = sizeof (p) * 8;
    set_image_options(img_opt);
}

void update_used_blocks_count(file_system_info* fs_info, unsigned long* bitmap) 
{
	unsigned long long used = 0;
	unsigned int i;

	for(i = 0; i < fs_info->totalblock; ++i) 
    {
        if (pc_test_bit(i, bitmap, fs_info->totalblock))
			++used;
	}

	fs_info->used_bitmap = used;
}
static unsigned long get_checksum_count(unsigned long long block_count, 
                                        uint32_t           blocks_per_cs) 
{

	if (blocks_per_cs == 0)
		return 0;
	else
		return block_count / blocks_per_cs;
}
ull convert_blocks_to_bytes(ull  block_offset, 
                            uint block_count, 
                            uint block_size,
						    uint blocks_per_checksum,
							uint checksum_size) 
{

	unsigned long long bytes_count = block_count * block_size;

	if (blocks_per_checksum) 
    {
		unsigned long total_cs  = get_checksum_count(block_offset + block_count, blocks_per_checksum);
		unsigned long copied_cs = get_checksum_count(block_offset, blocks_per_checksum);
		unsigned long newer_cs  = total_cs - copied_cs;
		bytes_count += newer_cs * checksum_size;
	}

	return bytes_count;
}
unsigned long long get_local_free_space(const char* path) 
{
	unsigned long long dest_size;
	struct statvfs stvfs;
	struct stat statP;

	if (statvfs(path, &stvfs) == -1) 
    {
		return 0;
	}
	/* if file is a FIFO there is no point in checking the size */
	if (!stat(path, &statP)) 
    {
		if (S_ISFIFO(statP.st_mode))
			return 0;
	} 
    else 
    {
	    return 0;
    }
	dest_size = (unsigned long long)stvfs.f_frsize * stvfs.f_bfree;
	if (!dest_size)
		dest_size = (unsigned long long)stvfs.f_bsize * stvfs.f_bfree;
    
    return dest_size;
}
/// get partition size
ull get_partition_free_space (int *fd) 
{
    ull dest_size = 0;
    unsigned long dest_block;
    struct stat stat;

    if (!fstat(*fd, &stat)) 
	{
        if (S_ISFIFO(stat.st_mode)) 
		{
            dest_size = 0;
        } 
		else if (S_ISREG(stat.st_mode)) 
		{
            dest_size = stat.st_size;
        } 
		else 
		{
#ifdef BLKGETSIZE64
            ioctl(*fd, BLKGETSIZE64, &dest_size);
            return dest_size;
#endif
#ifdef BLKGETSIZE
            ioctl(*fd, BLKGETSIZE, &dest_block);
            dest_size = (unsigned long long)(dest_block * 512);
            return dest_size;
#endif
        }
    } 
    return dest_size;
} 
static void init_image_head(image_head* image_hdr) 
{
	 memset(image_hdr, 0, sizeof(image_head));

    strncpy(image_hdr->magic, IMAGE_MAGIC, IMAGE_MAGIC_SIZE);
    strncpy(image_hdr->version, IMAGE_VERSION_0002, IMAGE_VERSION_SIZE);
    strncpy(image_hdr->ptc_version, VERSION, PARTCLONE_VERSION_SIZE);

    image_hdr->endianess = ENDIAN_MAGIC;
}

gboolean write_image_desc(int* fd, file_system_info fs_info,image_options img_opt) 
{
	image_desc image;
	init_image_head(&image.head);
	
	memcpy(&image.fs_info, &fs_info, sizeof(file_system_info));
	memcpy(&image.options, &img_opt, sizeof(image_options));

	init_crc32(&image.crc);
	image.crc = crc32(image.crc, &image, sizeof(image_desc) - CRC32_SIZE);
	if (write_read_io_all (fd, (char*)&image, sizeof(image_desc),WRITE) != sizeof(image_desc))
    {
        return FALSE;
    }    
    return TRUE;
}
gboolean write_image_bitmap(int *fd, 
                            file_system_info fs_info,
                            unsigned long* bitmap) 
{
	uint32_t crc;
	
	if (write_read_io_all(fd, (char*)bitmap, BITS_TO_BYTES(fs_info.totalblock),WRITE) == -1)
    {
        return FALSE;
    }    
	init_crc32(&crc);
	crc = crc32(crc, bitmap, BITS_TO_BYTES(fs_info.totalblock));
	if (write_read_io_all(fd, (char*)&crc, sizeof(crc),WRITE) != sizeof(crc))
    {
        return FALSE;
    }

    return TRUE;
}
void print_readable_size_str(unsigned long long size_byte, char *new_size_str) {

	float new_size = 1.0;
	memset(new_size_str, 0, 11);
	uint64_t tbyte=1000000000000.0;
	uint64_t gbyte=1000000000;
	uint64_t mbyte=1000000;
	uint64_t kbyte=1000;

	if (size_byte == 0)
		snprintf(new_size_str, 11, "%llu", size_byte);

	if (size_byte >= tbyte) {
		new_size = (float)size_byte / (float)tbyte;
		snprintf(new_size_str, 11, "%5.1f TB", new_size);
	} else if (size_byte >= gbyte) {
		new_size = (float)size_byte / (float)gbyte;
		snprintf(new_size_str, 11, "%5.1f GB", new_size);
	} else if (size_byte >= mbyte) {
		new_size = (float)size_byte / (float)mbyte;
		snprintf(new_size_str, 11, "%5.1f MB", new_size);
	} else if (size_byte >= kbyte) {
		new_size = (float)size_byte / (float)kbyte;
		snprintf(new_size_str, 11, "%5.1f KB", new_size);
	} else {
		snprintf(new_size_str, 11, "%3i Byte", (int)size_byte);
	}
}
void print_file_system_info(file_system_info fs_info) {

	unsigned int     block_s = fs_info.block_size;
	unsigned long long total = fs_info.totalblock;
	unsigned long long used  = fs_info.usedblocks;
	char size_str[11];

	print_readable_size_str(total*block_s, size_str);
	printf("Device size:  %s = %llu Blocks\n", size_str, total);

	print_readable_size_str(used*block_s, size_str);
	printf(("Space in use: %s = %llu Blocks\n"), size_str, used);

	print_readable_size_str((total-used)*block_s, size_str);
	printf(("Free Space:   %s = %llu Blocks\n"), size_str, (total-used));

	printf(("Block size:   %i Byte\n"), block_s);
}
gboolean read_image_desc(int              *fd,
		                 image_head       *img_head, 
					     file_system_info *fs_info, 
					     image_options    *img_opt) 
{

    image_desc image;
    int r_size;
    uint32_t crc;

    r_size = write_read_io_all (fd, (char*)&image, sizeof(image), READ);
    if (r_size != sizeof(image))
	{
		return FALSE;
	}
    // check the image magic
    if (memcmp(image.head.magic, IMAGE_MAGIC, IMAGE_MAGIC_SIZE))
	{
		return FALSE;
	}

    init_crc32(&crc);
    crc = crc32(crc, &image, sizeof(image) - CRC32_SIZE);
    if (crc != image.crc)
	{
		return FALSE;
	}
    if (image.head.endianess != ENDIAN_MAGIC)
	{
		return FALSE;
	}
    memcpy(fs_info, &(image.fs_info), sizeof(file_system_info));
    memcpy(img_opt, &(image.options), sizeof(image_options));
	memcpy(img_head, &(image.head),   sizeof(image_head));

	return TRUE;
}
gboolean load_image_bitmap_bits(int *fd,file_system_info fs_info, unsigned long *bitmap) 
{
    ull r_size, bitmap_size = BITS_TO_BYTES(fs_info.totalblock);
    uint32_t r_crc, crc;

    r_size = write_read_io_all (fd, (char*)bitmap, bitmap_size, READ);
    if (r_size != bitmap_size)
	{
		return FALSE;
	}

    r_size = write_read_io_all (fd, (char*)&r_crc, sizeof(r_crc), READ);
    if (r_size != sizeof(r_crc))
	{
		return FALSE;
	}
    init_crc32(&crc);
    crc = crc32(crc, bitmap, bitmap_size);
    if (crc != r_crc)
	{
		return FALSE;
	}

	return TRUE;
}
