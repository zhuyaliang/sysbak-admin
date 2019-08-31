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
#include "sysbak-extfs.h"
#include "sysbak-share.h"
#include "progress.h"
#include "bitmap.h"
#include "checksum.h"

#ifndef EXT2_FLAG_64BITS
#	define EXTFS_1_41 1.41
#endif

typedef struct 
{
    // Source Data File Descriptor
    int  dfr;   
    char *device;
    // Tagrger Data File Descriptor
    int  dfw;    
    char *targer;
    sysbak_progress progress_callback;
    gpointer p_data;
}sysdata;

/// open device
static ext2_filsys open_file_system (const char* device)
{
    errcode_t retval;
    int use_superblock = 0;
    int use_blocksize = 0;
    int flags;
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
static gboolean read_bitmap_info (const char* device, file_system_info fs_info, unsigned long* bitmap) 
{
    ext2_filsys extfs;
    errcode_t retval;
    unsigned long group;
    unsigned long long current_block, block;
    unsigned long long lfree, gfree;
    char *block_bitmap = NULL;
    int block_nbytes;
    unsigned long long blk_itr;
    int bg_flags = 0;
    int B_UN_INIT = 0;

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
			        pc_set_bit(current_block, bitmap, fs_info.totalblock);
		        } 
                else 
                {
		            lfree++;
		            gfree++;
		            pc_clear_bit(current_block, bitmap, fs_info.totalblock);
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
    
    extfs = open_file_system (device);
    if (extfs == NULL)
    {
        return FALSE;
    }    
    fs_info->block_size  = EXT2_BLOCK_SIZE(extfs->super);
    fs_info->totalblock  = (unsigned long long)ext2fs_blocks_count(extfs->super);
    fs_info->usedblocks  = (unsigned long long)(ext2fs_blocks_count(extfs->super) - 
                            ext2fs_free_blocks_count(extfs->super));
    fs_info->device_size = fs_info->block_size * fs_info->totalblock;

    ext2fs_close(extfs);

	return TRUE;
}
static gboolean check_system_space (file_system_info *fs_info,const char *targer)
{
	ull needed_space = 0;
	ull free_space = 0;
	
    needed_space += sizeof(image_head) + sizeof(file_system_info);
	needed_space += BITS_TO_BYTES(fs_info->totalblock);
	needed_space += convert_blocks_to_bytes(0, fs_info->usedblocks, 
			                                   fs_info->block_size,
											   fs_info->blocks_per_checksum,
											   fs_info->checksum_size);

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
static ull get_read_blocks_size (file_system_info *fs_info,ull *block_id,unsigned long *bitmap)
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
static gboolean read_write_data (file_system_info *fs_info,
                                 unsigned long    *bitmap,
								 int              *dfr,
								 int              *dfw)
{
	const uint block_size = fs_info->block_size; //Data size per block
	const uint buffer_capacity = DEFAULT_BUFFER_SIZE > block_size ? DEFAULT_BUFFER_SIZE / block_size : 1; // in blocks
	guchar checksum[fs_info->checksum_size];
	uint  blocks_in_cs = 0, blocks_per_cs, write_size;
	char *read_buffer, *write_buffer;
    ull   block_id;	
    int   r_size, w_size;	
    
    blocks_per_cs = fs_info->blocks_per_checksum;
	// Getting bytes of backup data
    write_size = convert_blocks_to_bytes(0, buffer_capacity, 
			                                block_size,
									        fs_info->blocks_per_checksum,
											fs_info->checksum_size);
	read_buffer = (char*)malloc(buffer_capacity * block_size);
	write_buffer = (char*)malloc(write_size + fs_info->checksum_size);
	if (read_buffer == NULL || write_buffer == NULL) 
	{
	    goto ERROR;
    }

	// read data from the first block
	if (lseek(*dfr, 0, SEEK_SET) == (off_t)-1)
	{
	    goto ERROR;
	}
	init_checksum(fs_info->checksum_mode, checksum);
	block_id = 0;
	do 
	{
	    ull i,blocks_read;
	    unsigned int cs_added = 0, write_offset = 0;
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
				memcpy(write_buffer + write_offset, checksum, fs_info->checksum_size);
				++cs_added;
				write_offset += fs_info->checksum_size;
				blocks_in_cs = 0;
				init_checksum(fs_info->checksum_mode, checksum);
			}
		}
		w_size = write_read_io_all(dfw, write_buffer, write_offset,WRITE);
		if (w_size != write_offset)
		{
	        goto ERROR;
		}
		block_id += blocks_read;
		if (r_size + cs_added * fs_info->checksum_size != w_size)
		{
	        goto ERROR;
		}
	} while (1);
	if (blocks_in_cs > 0) 
	{
		w_size = write_read_io_all(dfw, (char*)checksum, fs_info->checksum_size,WRITE);
		if (w_size != fs_info->checksum_size)
		{
			return FALSE;
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
static void start_sysbak_data (GTask         *task,
                               gpointer       source_object,
                               gpointer       d,
                               GCancellable  *cancellable)
{
    sysdata *data = (sysdata *)d;
	file_system_info fs_info;   /// description of the file system
    unsigned long *bitmap = NULL;
	GError *error = NULL;

	init_file_system_info(&fs_info);
     
    // get Super Block information from partition
	if (!read_super_blocks(data->device, &fs_info))
	{
		g_set_error_literal (&error,G_IO_ERROR ,G_IO_ERROR_NOT_SUPPORTED,"Couldn't find valid filesystem superblock");
		goto ERROR;
	}

	const unsigned int buffer_capacity = DEFAULT_BUFFER_SIZE > fs_info.block_size
				                        ? DEFAULT_BUFFER_SIZE / fs_info.block_size : 1; // in blocks
	fs_info.blocks_per_checksum = buffer_capacity;
   
	if (!check_memory_size(fs_info,buffer_capacity))
    {
        goto ERROR;
    }
	// alloc a memory to store bitmap
	bitmap = pc_alloc_bitmap(fs_info.totalblock);
	if (bitmap == NULL) 
    {
	    goto ERROR;;
    }
	
	read_bitmap_info(data->device, fs_info, bitmap);
	update_used_blocks_count(&fs_info, bitmap);
    if (!check_system_space (&fs_info,data->targer))
    {
        goto ERROR;
    }    
    write_image_desc(&(data->dfw), fs_info);
	write_image_bitmap(&(data->dfw), fs_info, bitmap);
    if (!read_write_data (&fs_info,bitmap,&(data->dfr),&(data->dfw)))
    {
        goto ERROR;
    } 

	g_task_return_pointer (task,&fs_info,NULL);
	g_object_unref (task);
	return;
ERROR:
	free(bitmap);
	g_task_return_error (task, error);
    g_object_unref (task);

}   
static void sys_data_free (sysdata *data)
{
	free(data->device);
	free(data->targer);
	close(data->dfr);
	close(data->dfw);
    g_slice_free (sysdata,data);
}
//Backup partition to image file 
/******************************************************************************
* Function:              sysbak_extfs_ptf      
*        
* Explain: Backup partition to image file eg: /dev/sda1 ---> sda1.img
*        
* Input:   @device       Partitions to be backed up
*          @targer       Name after backup
*          @overwrite    Overwrite existing backups
*        
* Output:  success      :TRUE
*          fail         :FALSE
*        
* Author:  zhuyaliang  29/08/2019
******************************************************************************/
gboolean sysbak_extfs_ptf_async (const char   *device,
                                 const char   *targer,
                                 gboolean      overwrite,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback finished_callback,
                                 gpointer      f_data,
                                 sysbak_progress progress_callback,
                                 gpointer      p_data)
{
    g_return_val_if_fail (device != NULL,FALSE);
    g_return_val_if_fail (targer != NULL,FALSE);
    g_return_val_if_fail (finished_callback != NULL,FALSE);
    GTask *task;
    sysdata *data;
    int dfr,dfw;

	dfr = open_source_device(device,BACK_PTF);
	if (dfr <= 0 ) 
    {
		return FALSE;
	}
	dfw = open_target_device(targer,BACK_PTF,overwrite);
	if (dfw <= 0 ) 
    {
		return FALSE;
	}
    
    data = g_slice_new (sysdata);
    data->dfr = dfr;
    data->dfw = dfw;
    data->progress_callback = progress_callback;
    data->p_data = p_data;
    data->device = g_strdup (device);
    data->targer = g_strdup (targer);
    
    task = g_task_new (NULL,cancellable,finished_callback,f_data);
    g_task_set_task_data (task, data, (GDestroyNotify) sys_data_free);
    g_task_run_in_thread (task, start_sysbak_data);
    
    return TRUE;      /// finish
}

file_system_info *sysbak_extfs_ptf_finish (GAsyncResult  *result,
                                           GError       **error) 
{
	g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (result), error);
}
//Backup partition to partition 
gboolean sysbak_extfs_ptp (const char *s_device,
                           const char *t_device,
                           gboolean    overwrite)
{

    return TRUE;
}    
