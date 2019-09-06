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

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ext2fs/ext2fs.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>

#define in_use(m, x)    (ext2fs_test_bit ((x), (m)))
#include "gdbus-extfs.h"
#include "gdbus-share.h"
#include "checksum.h"
#include "gdbus-bitmap.h"
#include "progress.h"

#ifndef EXT2_FLAG_64BITS
#	define EXTFS_1_41 1.41
#endif
const char *sysbak_error_message[10] = 
{
	"Device been mounted",
	"Failed to open source file",
	"Failed to open target file",
	"Failed to read superblock",
	"Not enough memory",
	"Failed to read bitmap",
	"Not enough disk  space",
	"Write header information failed",
	"Failed reading and writing data"
	"Failed reading image header"
};
static ull copied_count;
// open device
static ext2_filsys open_file_system (const char* device)
{
    errcode_t   retval;
    int         use_superblock = 0;
    int         use_blocksize = 0;
    int         flags;
    ext2_filsys fs;

#ifdef EXTFS_1_41
    flags = EXT2_FLAG_JOURNAL_DEV_OK | EXT2_FLAG_SOFTSUPP_FEATURES;
#else
    flags = EXT2_FLAG_JOURNAL_DEV_OK | EXT2_FLAG_SOFTSUPP_FEATURES | EXT2_FLAG_64BITS;
#endif

    retval = ext2fs_open (device, 
                          flags, 
                          use_superblock, 
                          use_blocksize, 
                          unix_io_manager, 
                         &fs);

    if (retval)
    {
        return NULL;
    }    

    ext2fs_mark_valid(fs);
    if ((fs->super->s_state & EXT2_ERROR_FS) || !ext2fs_test_valid(fs))
    {
        return NULL;
    }    
    else if ((fs->super->s_state & EXT2_VALID_FS) == 0)
    {
        return NULL;
    }    
    else if ((fs->super->s_max_mnt_count > 0) && 
            (fs->super->s_mnt_count >= (unsigned) fs->super->s_max_mnt_count)) 
    {
        return NULL;
    }

    return fs;
}

// reference dumpe2fs
static gboolean read_bitmap_info (const char      *device, 
		                          file_system_info fs_info, 
								  ul              *bitmap) 
{
    ext2_filsys extfs;
    errcode_t   retval;
    ul          group;
    ull         current_block, block;
    ull         lfree, gfree;
    char       *block_bitmap = NULL;
    int         block_nbytes;
    ull         blk_itr;
    int         bg_flags = 0;
    int         B_UN_INIT = 0;

    extfs = open_file_system(device);
    if (extfs == NULL)
    {
        return FALSE;
    }    
    retval = ext2fs_read_bitmaps(extfs); /// open extfs bitmap
    if (retval)
    {
        return FALSE;
    }    

    block_nbytes = EXT2_BLOCKS_PER_GROUP(extfs->super) / 8;
    if (extfs->block_map)
        block_bitmap = malloc(block_nbytes);

    // initial image bitmap as 1 (all block are used)
    pc_init_bitmap(bitmap, 0xFF, fs_info.totalblock);

    lfree = 0;
    current_block = 0;
    blk_itr = extfs->super->s_first_data_block;

    /// each group
    for (group = 0; group < extfs->group_desc_count; group++)
    {
        gfree = 0;
        B_UN_INIT = 0;

        if (block_bitmap) 
        {
#ifdef EXTFS_1_41
            ext2fs_get_block_bitmap_range(extfs->block_map, 
                                          blk_itr, 
                                          block_nbytes << 3, 
                                          block_bitmap);
#else
            ext2fs_get_block_bitmap_range2(extfs->block_map, 
                                           blk_itr, 
                                           block_nbytes << 3, 
                                           block_bitmap);
#endif
        if (extfs->super->s_feature_ro_compat & EXT4_FEATURE_RO_COMPAT_GDT_CSUM)
        {
#ifdef EXTFS_1_41
            bg_flags = extfs->group_desc[group].bg_flags;
#else
            bg_flags = ext2fs_bg_flags(extfs, group);
#endif
            if (bg_flags&EXT2_BG_BLOCK_UNINIT)
            {
                B_UN_INIT = 1;
            } 
        }
        // each block in group
        for (block = 0; ((block < extfs->super->s_blocks_per_group) && 
                    (current_block < (fs_info.totalblock-1))); 
                block++) 
        {
            current_block = block + blk_itr;

            if (in_use (block_bitmap, block))
            {
                if (!pc_set_bit(current_block, bitmap, fs_info.totalblock))
                {
                    return FALSE;
                }    
            } 
            else 
            {
                lfree++;
                gfree++;
                if (!pc_clear_bit(current_block, bitmap, fs_info.totalblock))
                {
                    return FALSE;
                }    
            }

        }
        blk_itr += extfs->super->s_blocks_per_group;
    }
    /// check free blocks in group
#ifdef EXTFS_1_41
    if (gfree != extfs->group_desc[group].bg_free_blocks_count)
    {	
#else
        if (gfree != ext2fs_bg_free_blocks_count(extfs, group))
        {
#endif
            if (!B_UN_INIT)
                return FALSE;
        }
    }
    /// check all free blocks in partition
    if (lfree != ext2fs_free_blocks_count(extfs->super)) 
    {
        return FALSE;
    }

    ext2fs_close(extfs);
    free(block_bitmap);

    return TRUE;
}
static gboolean read_super_blocks(const char* device, file_system_info* fs_info)
{
    ext2_filsys extfs;

    strncpy(fs_info->fs, extfs_MAGIC, FS_MAGIC_SIZE);
    extfs = open_file_system (device);
    if (extfs == NULL)
    {
        return FALSE;
    }    
    fs_info->block_size  = EXT2_BLOCK_SIZE(extfs->super);
    fs_info->totalblock  = (ull)ext2fs_blocks_count(extfs->super);
    fs_info->usedblocks  = (ull)(ext2fs_blocks_count(extfs->super) - 
                                 ext2fs_free_blocks_count(extfs->super));
    fs_info->device_size = fs_info->block_size * fs_info->totalblock;

    ext2fs_close(extfs);

    return TRUE;
}
static gboolean check_system_space (file_system_info *fs_info,
                                    const char       *targer,
                                    image_options    *img_opt)
{
    ull needed_space = 0;
    ull free_space = 0;

    needed_space += sizeof(image_head) + sizeof(file_system_info) + sizeof(image_options);
    needed_space += BITS_TO_BYTES(fs_info->totalblock);
    needed_space += convert_blocks_to_bytes(0, fs_info->usedblocks, 
            fs_info->block_size,
            img_opt->blocks_per_checksum,
            img_opt->checksum_size);
    free_space = get_local_free_space(targer);
    if (free_space < needed_space)
    {
        return FALSE;
    }    

    return TRUE;
}   
//Backup partition to image file 
/******************************************************************************
 * Function:              get_read_blocks_size      
 *        
 * Explain: Get the block size for each read
 *        
 * Input:   @fs_info      
 *          @block_id   ID of current block
 *          @overwrite  File System Bitmap
 *        
 * Output:  success      :block size
 *          fail         :-1
 *        
 * Author:  zhuyaliang  30/08/2019
 ******************************************************************************/
static ull get_read_blocks_size (file_system_info *fs_info,ull *block_id,ul *bitmap)
{
    ull blocks_skip,blocks_read;
    const ull  blocks_total = fs_info->totalblock;
    const uint block_size = fs_info->block_size;
    const uint buffer_capacity = DEFAULT_BUFFER_SIZE > block_size ? DEFAULT_BUFFER_SIZE / block_size : 1;

    /// skip unused blocks
    for (blocks_skip = 0;
            *block_id + blocks_skip < blocks_total &&
            !pc_test_bit(*block_id + blocks_skip, bitmap, blocks_total);
            blocks_skip++);
    if (*block_id + blocks_skip == blocks_total)
    {
        return 0;
    }

    if (blocks_skip)
    {    
        *block_id += blocks_skip;
    }
    /// read blocks size < 256 && Current block has data
    for (blocks_read = 0;
            *block_id + blocks_read < blocks_total && blocks_read < buffer_capacity &&
            pc_test_bit(*block_id + blocks_read, bitmap,blocks_total);
            ++blocks_read);

    return blocks_read;

}    
static gboolean read_write_data_ptf (IoGdbus          *object,
		                             file_system_info *fs_info,
                                     image_options    *img_opt,
                                     unsigned long    *bitmap,
                                     int              *dfr,
                                     int              *dfw)
{
    const uint block_size = fs_info->block_size; //Data size per block
    const uint buffer_capacity = DEFAULT_BUFFER_SIZE > block_size ? 
        DEFAULT_BUFFER_SIZE / block_size : 1; // in blocks
    guchar checksum[img_opt->checksum_size];
    uint  blocks_in_cs = 0, blocks_per_cs, write_size;
    char *read_buffer, *write_buffer;
    ull   block_id = 0;	
    int   r_size, w_size;	
	progress_bar  prog;
    progress_data pdata;

	progress_init(&prog, 0, fs_info->usedblocks, fs_info->block_size);
    blocks_per_cs = img_opt->blocks_per_checksum;
    // Getting bytes of backup data
    write_size = convert_blocks_to_bytes(0, buffer_capacity, 
            block_size,
            img_opt->blocks_per_checksum,
            img_opt->checksum_size);
    read_buffer = (char*)malloc(buffer_capacity * block_size);
    write_buffer = (char*)malloc(write_size + img_opt->checksum_size);
    if (read_buffer == NULL || write_buffer == NULL) 
    {
        goto ERROR;
    }
    // read data from the first block
    if (lseek(*dfr, 0, SEEK_SET) == (off_t)-1)
    {  
        goto ERROR;
    }
    init_checksum(img_opt->checksum_mode, checksum);
    do 
    {
        ull i,blocks_read;
        int cs_added = 0, write_offset = 0;
        off_t offset;
        blocks_read = get_read_blocks_size (fs_info,&block_id,bitmap);
        if (blocks_read == 0)
            break;

        offset = (off_t)(block_id * block_size);
        if (lseek(*dfr, offset, SEEK_SET) == (off_t)-1)
        {
            goto ERROR;
        }
        r_size = write_read_io_all (dfr, read_buffer, blocks_read * block_size,READ);
        if (r_size != (int)(blocks_read * block_size)) 
        {
            goto ERROR;
        }
        for (i = 0; i < blocks_read; ++i) 
        {
            memcpy(write_buffer + write_offset,
                   read_buffer + i * block_size, 
                   block_size);
            write_offset += block_size;
            update_checksum(checksum, read_buffer + i * block_size, block_size);
            if (blocks_per_cs > 0 && ++blocks_in_cs == blocks_per_cs)
            {
                memcpy(write_buffer + write_offset, checksum, img_opt->checksum_size);
                ++cs_added;
                write_offset += img_opt->checksum_size;
                blocks_in_cs = 0;
                init_checksum(img_opt->checksum_mode, checksum);
            }
        }
        w_size = write_read_io_all(dfw, write_buffer, write_offset,WRITE);
        if (w_size != write_offset)
        {
            goto ERROR;
        }
        block_id += blocks_read;
        copied_count += blocks_read;
		if (!progress_update(&prog, copied_count,&pdata))
		{
			pdata.percent=100.0;
			pdata.remained = (time_t)0;
		}
		io_gdbus_emit_sysbak_progress (object,
				                       pdata.percent,
									   pdata.speed,
									   pdata.remained,
									   pdata.elapsed);
        if (r_size + cs_added * img_opt->checksum_size != w_size)
        {
            goto ERROR;
        }
		//io_gdbus_set_read_size (object,block_id);
    } while (1);
    if (blocks_in_cs > 0) 
    {
        w_size = write_read_io_all(dfw, (char*)checksum, img_opt->checksum_size,WRITE);
        if (w_size != img_opt->checksum_size)
        {
            goto ERROR;
        }
    }
    free(write_buffer);
    free(read_buffer);
    return TRUE;
ERROR:
    if (read_buffer != NULL)
    {
        free (read_buffer);
    }
    if (write_buffer != NULL)
    {
        free (write_buffer);
    }
    return FALSE;	
}
gboolean gdbus_sysbak_extfs_ptf (IoGdbus               *object,
                                 GDBusMethodInvocation *invocation,
								 const gchar           *source,
								 const gchar           *target,
                                 gboolean               overwrite)
{
    file_system_info fs_info;   /// description of the file system
    image_options    img_opt;
    unsigned long   *bitmap = NULL;
    uint             buffer_capacity;
	int              e_code;
	gint             dfr = 0,dfw = 0;

    dfr = open_source_device(source,BACK_PTF);
    if (dfr <= 0 ) 
    {
		if (dfr == -2)
		{
			e_code = 0;
		}
		else
		{
			e_code = 1;
		}
        goto ERROR;;
    }
    dfw = open_target_device(target,BACK_PTF,overwrite);
    if (dfw <= 0 ) 
    {
		e_code = 2;
        goto ERROR;;
    }
    init_file_system_info(&fs_info);
    init_image_options(&img_opt);
    
    // get Super Block information from partition
    if (!read_super_blocks(source, &fs_info))
    {
		e_code = 3;
        goto ERROR;
    }

    buffer_capacity = DEFAULT_BUFFER_SIZE > fs_info.block_size
                    ? DEFAULT_BUFFER_SIZE / fs_info.block_size : 1; // in blocks
    img_opt.blocks_per_checksum = buffer_capacity; 
    if (!check_memory_size(fs_info,img_opt))
    {
		e_code = 4;
        goto ERROR;
    }
    // alloc a memory to store bitmap
    bitmap = pc_alloc_bitmap(fs_info.totalblock);
    if (bitmap == NULL) 
    {
		e_code = 4;
        goto ERROR;
    }
    if (!read_bitmap_info(source, fs_info, bitmap))
    {
		e_code = 5;
        goto ERROR;
    }    
    update_used_blocks_count(&fs_info, bitmap);
    if (!check_system_space (&fs_info,target,&img_opt))
    {
		e_code = 6;
        goto ERROR;
    }    
    if (!write_image_desc(&dfw, fs_info,img_opt))
    {
		e_code = 7;
        goto ERROR;
    }    
    write_image_bitmap(&dfw, fs_info, bitmap);
    copied_count = 0;
	io_gdbus_complete_sysbak_extfs_ptf (object,invocation); 
    if (!read_write_data_ptf (object,&fs_info,&img_opt,bitmap,&dfr,&dfw))
	{
		e_code = 8;
        goto ERROR;
    } 

    fsync(dfw);
	io_gdbus_emit_sysbak_finished (object,
								   fs_info.totalblock,
								   fs_info.usedblocks,
								   fs_info.block_size);
	free(bitmap);
    close (dfw);
    close (dfr);
    return TRUE;
ERROR:
	io_gdbus_complete_sysbak_extfs_ptf (object,invocation); 
	io_gdbus_emit_sysbak_error (object,
							    sysbak_error_message[e_code],
			                    e_code);
    free(bitmap);
    if (dfr > 0)
    {    
        close (dfr);
    }    
    if (dfw > 0) 
    {    
        close (dfw);
    }    
	return FALSE;
}   

static gboolean read_write_data_ptp (IoGdbus          *object,
	                                file_system_info *fs_info,
                                    ul               *bitmap,
                                    int              *dfr,
                                    int              *dfw)
{
    const uint block_size = fs_info->block_size; //Data size per block
    const uint buffer_capacity = DEFAULT_BUFFER_SIZE > block_size ? 
		                         DEFAULT_BUFFER_SIZE / block_size : 1; // in blocks
    char *buffer;
    ull   block_id = 0;	
    int   r_size, w_size;	
	progress_bar  prog;
    progress_data pdata;

	progress_init(&prog, 0, fs_info->usedblocks, fs_info->block_size);
    buffer = (char*)malloc(buffer_capacity * block_size);
    if (buffer == NULL) 
    {
        goto ERROR;
    }
    // read data from the first block
    if (lseek(*dfr, 0, SEEK_SET) == (off_t)-1)
    {
        goto ERROR;
    }
    do 
    {
        ull blocks_read;
        off_t offset;

        blocks_read = get_read_blocks_size (fs_info,&block_id,bitmap);
        if (blocks_read == 0)
            break;

        offset = (off_t)(block_id * block_size);
        if (lseek(*dfr, offset, SEEK_SET) == (off_t)-1)
        {
            goto ERROR;
        }
        if (lseek(*dfw, offset, SEEK_SET) == (off_t)-1)
        {
            goto ERROR;
        }
        r_size = write_read_io_all (dfr, buffer, blocks_read * block_size,READ);
        if (r_size != (int)(blocks_read * block_size)) 
        {
            goto ERROR;
        }
        w_size = write_read_io_all(dfw, buffer, blocks_read * block_size,WRITE);
        if (w_size != (int)(blocks_read * block_size))
        {
            goto ERROR;
        }
        block_id += blocks_read;
		if (!progress_update(&prog, block_id,&pdata))
		{
			pdata.percent=100.0;
			pdata.remained = (time_t)0;
		}
		io_gdbus_emit_sysbak_progress (object,
				                       pdata.percent,
									   pdata.speed,
									   pdata.remained,
									   pdata.elapsed);
    } while (1);

    free(buffer);
    return TRUE;
ERROR:
    if (buffer != NULL)
    {
        free (buffer);
    }
    return FALSE;	
}

gboolean gdbus_sysbak_extfs_ptp (IoGdbus               *object,
                                 GDBusMethodInvocation *invocation,
								 const gchar           *source,
								 const gchar           *target,
                                 gboolean               overwrite)
{
    file_system_info fs_info;   /// description of the file system
    image_options    img_opt;
    ul              *bitmap = NULL;
    uint             buffer_capacity;
    ull free_space = 0;
    gint             e_code;
	gint             dfr = 0,dfw = 0;

    dfr = open_source_device(source,BACK_PTP);
    if (dfr <= 0 ) 
    {
        e_code = 1;
        goto ERROR;;
    }
    dfw = open_target_device(target,BACK_PTP,overwrite);
    if (dfw <= 0 ) 
    {
        e_code = 2;
        goto ERROR;;
    }

    init_file_system_info(&fs_info);
    init_image_options(&img_opt);
    // get Super Block information from partition
    if (!read_super_blocks(source, &fs_info))
    {
        e_code = 3;
        goto ERROR;
    }

    buffer_capacity = DEFAULT_BUFFER_SIZE > fs_info.block_size
                    ? DEFAULT_BUFFER_SIZE / fs_info.block_size : 1; // in blocks
    img_opt.blocks_per_checksum = buffer_capacity;

    if (!check_memory_size(fs_info,img_opt))
    {
        e_code = 4;
        goto ERROR;
    }
    // alloc a memory to store bitmap
    bitmap = pc_alloc_bitmap(fs_info.totalblock);
    if (bitmap == NULL) 
    {
        e_code = 4;
        goto ERROR;
    }

    if (!read_bitmap_info(source, fs_info, bitmap))
    {
        e_code = 5;
        goto ERROR;
    }    
    free_space = get_partition_free_space(&dfw);
    if (free_space < fs_info.device_size)
    {
        e_code = 6;
        goto ERROR;
    }   
    copied_count = 0;
	io_gdbus_complete_sysbak_extfs_ptp (object,invocation); 
    if (!read_write_data_ptp (object,
				              &fs_info,
                              bitmap,
                              &dfr,
                              &dfw))
    {
        e_code = 8;
        goto ERROR;
    }

    fsync(dfw);
    free(bitmap);
    close (dfr);
    close (dfw);
	io_gdbus_emit_sysbak_finished (object,
								   fs_info.totalblock,
								   fs_info.usedblocks,
								   fs_info.block_size);
    return TRUE;
ERROR:
	io_gdbus_complete_sysbak_extfs_ptp (object,invocation); 
    free(bitmap);
    if (dfr > 0)
    {    
        close (dfr);
    }    
    if (dfw > 0)   
    {    
        close (dfw);
    }    
	io_gdbus_emit_sysbak_error (object,
							    sysbak_error_message[e_code],
			                    e_code);
	return FALSE;
}   
static gboolean read_write_data_restore (IoGdbus          *object,
		                                 file_system_info *fs_info,
                                         image_options    *img_opt,
                                         ul               *bitmap,
                                         int              *dfr,
                                         int              *dfw)
{
    const ull  blocks_total = fs_info->totalblock;
    const uint block_size = fs_info->block_size; //Data size per block
    const uint buffer_capacity = DEFAULT_BUFFER_SIZE > block_size ? DEFAULT_BUFFER_SIZE / block_size : 1; // in blocks
    const uint blocks_per_cs = img_opt->blocks_per_checksum;
    ull    blocks_used = fs_info->usedblocks;
    uint   blocks_in_cs = 0, buffer_size, read_offset;
    guchar checksum[img_opt->checksum_size];
    char  *read_buffer = NULL, *write_buffer = NULL;
    ull    block_id;    
    ull    blocks_used_fix = 0, test_block = 0;
    int    r_size, w_size;  
	progress_bar  prog;
    progress_data pdata;

	progress_init(&prog, 0, fs_info->usedblocks, fs_info->block_size);

    // fix some super block record incorrect
    for (test_block = 0; test_block < blocks_total; ++test_block)
        if (pc_test_bit(test_block, bitmap, fs_info->totalblock))
            blocks_used_fix++;

    if (blocks_used_fix != blocks_used)
    {    
        blocks_used = blocks_used_fix;
    }

    buffer_size = convert_blocks_to_bytes(0, buffer_capacity,
            block_size,
            img_opt->blocks_per_checksum,
            img_opt->checksum_size);
    read_buffer = (char*)malloc(buffer_size);
#define BSIZE 512
    posix_memalign((void**)&write_buffer, BSIZE, buffer_capacity * block_size);
    if (read_buffer == NULL || write_buffer == NULL)
    {
        goto ERROR;
    }

    // read data from the first block
    if (lseek(*dfw, 0, SEEK_SET) == (off_t)-1)
    {
        goto ERROR;
    }
    init_checksum(img_opt->checksum_mode, checksum);
    block_id = 0;
    do
    {
        unsigned int i;
        ull blocks_written, bytes_skip;
        int read_size;
        // max chunk to read using one read(2) syscall
        uint blocks_read = copied_count + buffer_capacity < blocks_used ?
            buffer_capacity : blocks_used - copied_count;
        if (!blocks_read)
            break;
        read_size = convert_blocks_to_bytes(copied_count,
                blocks_read,
                block_size,
                img_opt->blocks_per_checksum,
                img_opt->checksum_size);
        // increase read_size to make room for the oversized checksum
        if (blocks_per_cs && blocks_read < buffer_capacity &&
                (blocks_read % blocks_per_cs) && (blocks_used % blocks_per_cs))
        {
            read_size += img_opt->checksum_size;
        }
        r_size = write_read_io_all (dfr, read_buffer, read_size,READ);
        if (r_size != read_size)
        {
            goto ERROR;
        }
        read_offset = 0;
        for (i = 0; i < blocks_read; ++i)
        {

            memcpy(write_buffer + i * block_size,
                    read_buffer + read_offset,
                    block_size);
            update_checksum(checksum, read_buffer + read_offset, block_size);
            if (++blocks_in_cs == blocks_per_cs)
            {
                guchar checksum_orig[img_opt->checksum_size];
                memcpy(checksum_orig, read_buffer + read_offset + block_size, img_opt->checksum_size);
                if (memcmp(read_buffer + read_offset + block_size, checksum, img_opt->checksum_size))
                {
                    goto ERROR;
                }
             read_offset += img_opt->checksum_size;
                blocks_in_cs = 0;
                init_checksum(img_opt->checksum_mode, checksum);
            }

            read_offset += block_size;
        }
        if (blocks_in_cs && blocks_per_cs && blocks_read < buffer_capacity &&
                (blocks_read % blocks_per_cs))
        {
            if (memcmp(read_buffer + read_offset, checksum, img_opt->checksum_size))
            {
                goto ERROR;
            }
        }
        blocks_written = 0;
        do
        {
            uint blocks_write = 0;

            // count bytes to skip
            for (bytes_skip = 0;
                    block_id < blocks_total &&
                    !pc_test_bit(block_id, bitmap, fs_info->totalblock);
                    block_id++, bytes_skip += block_size);

            // skip empty blocks
           if (blocks_write == 0)
            {
                if (bytes_skip > 0 && lseek(*dfw, (off_t)bytes_skip, SEEK_CUR) == (off_t)-1)
                {
                    goto ERROR;
                }
            }
            // blocks to write
            for (blocks_write = 0;
                    block_id + blocks_write < blocks_total &&
                    blocks_written + blocks_write < blocks_read &&
                    pc_test_bit(block_id + blocks_write, bitmap, fs_info->totalblock);
                    blocks_write++);

            // write blocks
            if (blocks_write > 0)
            {
                w_size = write_read_io_all (dfw,
                        write_buffer + blocks_written * block_size,
                        blocks_write * block_size,
                        WRITE);
                if (w_size != (int)blocks_write * (int)block_size)
                {
                    goto ERROR;
                }
            }
            blocks_written += blocks_write;
            block_id += blocks_write;
            copied_count += blocks_write;
			if (!progress_update(&prog, copied_count,&pdata))
			{
				pdata.percent=100.0;
				pdata.remained = (time_t)0;
			}
			io_gdbus_emit_sysbak_progress (object,
										   pdata.percent,
									       pdata.speed,
									       pdata.remained,
										   pdata.elapsed);
        } while (blocks_written < blocks_read);
    } while(1);

    free(write_buffer);
    free(read_buffer);
    return TRUE;
ERROR:
    if (read_buffer != NULL)
    {
        free (read_buffer);
    }
    if (write_buffer != NULL)
    {
        free (write_buffer);
    }
    return FALSE;
}

gboolean gdbus_sysbak_restore (IoGdbus               *object,
                               GDBusMethodInvocation *invocation,
							   const char            *source,
							   const char            *target,
                               gboolean               overwrite)
{
    file_system_info fs_info;   /// description of the file system
    image_options    img_opt;
    image_head       img_head;
    ul              *bitmap = NULL;
    ull              free_space;
    int              e_code;
	gint             dfr = 0,dfw = 0;

    dfr = open_source_device(source,RESTORE);
    if (dfr <= 0 ) 
    {
        e_code = 1;
        goto ERROR;;
    }
    dfw = open_target_device(target,RESTORE,overwrite);
    if (dfw <= 0 ) 
    {
        e_code = 2;
        goto ERROR;;
    }
    
    init_file_system_info(&fs_info);
    init_image_options(&img_opt);
    if (!read_image_desc(&dfr, &img_head, &fs_info, &img_opt))
    {
        e_code = 9;
        goto ERROR;
    }

    if (!check_memory_size(fs_info,img_opt))
    {
        e_code = 4;
        goto ERROR;
    }
    // alloc a memory to store bitmap
    bitmap = pc_alloc_bitmap(fs_info.totalblock);
    if (bitmap == NULL) 
    {
        e_code = 4;
        goto ERROR;;
    }
    if (!load_image_bitmap_bits(&dfr, fs_info, bitmap))
    {
        e_code = 5;
        goto ERROR;
    }    
    free_space = get_partition_free_space(&dfw);
    if (free_space < fs_info.device_size)
    {
        e_code = 6;
        goto ERROR;
    }  
    copied_count = 0;
	io_gdbus_complete_sysbak_restore (object,invocation);
    if (!read_write_data_restore (object,
				                  &fs_info,
								  &img_opt,
								  bitmap,
								  &dfr,
								  &dfw))
    {
        e_code = 8;
        goto ERROR;
    } 
    
    fsync(dfw);
    free(bitmap);
    close (dfw);
    close (dfr);
	io_gdbus_emit_sysbak_finished (object,
								   fs_info.totalblock,
								   fs_info.usedblocks,
								   fs_info.block_size);
    return TRUE;
ERROR:
	io_gdbus_complete_sysbak_restore (object,invocation);
    free(bitmap);
    if (dfr > 0)
    {    
        close (dfr);
    }    
    if (dfw > 0)    
    {    
        close (dfw);
    }    
	io_gdbus_emit_sysbak_error (object,
							    sysbak_error_message[e_code],
			                    e_code);
	return FALSE;
}   
gboolean gdbus_get_extfs_device_info (IoGdbus               *object,
		                              GDBusMethodInvocation *invocation,
									  const char            *device)
{
    ext2_filsys extfs;
	ull totalblock,usedblocks;
	uint block_size;

    extfs = open_file_system (device);
    if (extfs == NULL)
    {
        return FALSE;
    }    
    block_size  = EXT2_BLOCK_SIZE(extfs->super);
    totalblock  = (ull)ext2fs_blocks_count(extfs->super);
    usedblocks  = (ull)(ext2fs_blocks_count(extfs->super) - 
                        ext2fs_free_blocks_count(extfs->super));

    ext2fs_close(extfs);
	io_gdbus_complete_get_extfs_device_info (object,
			                                 invocation,
											 totalblock,
											 usedblocks,
											 block_size);	
    return TRUE;
}	
gboolean gdbus_get_extfs_image_info (IoGdbus               *object,
		                             GDBusMethodInvocation *invocation,
									 const char            *image_name)
{
    file_system_info fs_info;   /// description of the file system
    image_options    img_opt;
    image_head       img_head;
    int fd;

    fd  = open_source_device(image_name,RESTORE);
    if (fd <= 0 ) 
    {
        return FALSE;
    }
    init_file_system_info(&fs_info);
    init_image_options(&img_opt);
    if (!read_image_desc(&fd, 
                         &img_head, 
                         &fs_info, 
                         &img_opt))
    {
        close (fd);
        return FALSE;
    }    
 
	io_gdbus_complete_get_extfs_image_info (object,
			                                invocation,
										    fs_info.totalblock,
										    fs_info.usedblocks,
										    fs_info.block_size);	
    close (fd);
    return TRUE;
}	
