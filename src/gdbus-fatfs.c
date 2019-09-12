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

#include "gdbus-fatfs.h"
#include "gdbus-share.h"
#include "checksum.h"
#include "gdbus-bitmap.h"
#include "progress.h"

#define FAT12_THRESHOLD        4085
#define FAT16_THRESHOLD        65525
#define GET_UNALIGNED_W(f)     ( (uint16_t)f[0] | ((uint16_t)f[1]<<8) )
#define ROUND_TO_MULTIPLE(n,m) ((n) && (m) ? (n)+(m)-1-((n)-1)%(m) : 0)
#define MSDOS_DIR_BITS         5        /* log2(sizeof(struct msdos_dir_entry)) */

static int FS;
const char *sysbak_error_message[10] = 
{
	"Device Busy",
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
static ull get_total_sector(FatBootSector *fat_sb)
{
    ull total_sector = 0;

    /// get fat sectors
    if (fat_sb->sectors != 0)
        total_sector = (ull)fat_sb->sectors;
    else if (fat_sb->sector_count != 0)
        total_sector = (ull)fat_sb->sector_count;
    
    return total_sector;

}
/// get_used_block - get FAT used blocks
static ull get_used_block()
{
    ull i = 0;
    int fat_stat = 0;
    ull block = 0, bfree = 0, bused = 0, DamagedClusters = 0;
    ull cluster_count = 0, total_sector = 0;
    ull real_back_block= 0;
    int FatReservedBytes = 0;
    unsigned long *fat_bitmap;

    log_mesg(2, 0, 0, fs_opt.debug, "%s: get_used_block start\n", __FILE__);

    total_sector = get_total_sector();
    cluster_count = get_cluster_count();

    fat_bitmap = pc_alloc_bitmap(total_sector);
    if (fat_bitmap == NULL)
        log_mesg(2, 1, 1, fs_opt.debug, "%s: bitmapalloc error\n", __FILE__);
    pc_init_bitmap(fat_bitmap, 0xFF, total_sector);

    /// A) B) C)
    block = mark_reserved_sectors(fat_bitmap, block);

    /// D) The clusters
    FatReservedBytes = fat_sb.sector_size * fat_sb.reserved;

    /// The first fat will be seek
    if (lseek(ret, FatReservedBytes, SEEK_SET) == -1)
    log_mesg(0, 1, 1, fs_opt.debug, "%s, %i, ERROR: seek FatReservedBytes, error\n", __func__, __LINE__);

    /// The second fat is used to check FAT status
    fat_stat = check_fat_status();
    if (fat_stat == 1)
        log_mesg(0, 1, 1, fs_opt.debug, "%s: Filesystem isn't in valid state. May be it is not cleanly unmounted.\n\n"    , __FILE__);
    else if (fat_stat == 2)
        log_mesg(0, 1, 1, fs_opt.debug, "%s: I/O error! %X\n", __FILE__);

    for (i=0; i < cluster_count; i++){
    if (block >= total_sector)
            log_mesg(1, 0, 0, fs_opt.debug, "%s: error block too large\n", __FILE__);
        /// If FAT16
        if(FS == FAT_16){
            block = check_fat16_entry(fat_bitmap, block, &bfree, &bused, &DamagedClusters);
        } else if (FS == FAT_32){ /// FAT32
            block = check_fat32_entry(fat_bitmap, block, &bfree, &bused, &DamagedClusters);
        } else if (FS == FAT_12){ /// FAT12
            block = check_fat12_entry(fat_bitmap, block, &bfree, &bused, &DamagedClusters);
        } else
            log_mesg(2, 0, 0, fs_opt.debug, "%s: error fs\n", __FILE__);
    }

    while(block < total_sector){
        pc_set_bit(block, fat_bitmap, total_block);
        block++;
    }


    for (block = 0; block < total_sector; block++)
    {
        if (pc_test_bit(block, fat_bitmap, total_block)) {
            real_back_block++;
        }
    }
    free(fat_bitmap);
    log_mesg(2, 0, 0, fs_opt.debug, "%s: get_used_block down\n", __FILE__);

    return real_back_block;
}

// open device
static gboolean get_fat_fs_sector (const char* device,FatBootSector *fat_sb)
{
    char *buffer;
    int fd;

    fd = open(device, O_RDONLY);
    if (fd < 0 )
    {
        return FALSE;
    }     
    buffer = (char*)malloc(sizeof(FatBootSector));
    if(buffer == NULL)
    {
        close (fd);
        return FALSE; 
    }
    if(read (fd, buffer, sizeof(FatBootSector)) != sizeof(FatBootSector))
    {
        close (fd);
        free(buffer);
        return FALSE;
    }    
    assert(buffer != NULL);
    memcpy(fat_sb, buffer, sizeof(FatBootSector));
    free(buffer);
    close (fd);
    
    return TRUE;
}

static gboolean get_fat_fs_info (const char* device,FatFsInfo *fatfs_info)
{
    char *buffer;
    int fd;

    fd = open(device, O_RDONLY);
    if (fd < 0 )
    {
        return FALSE;
    }     
    buffer = (char*)malloc(sizeof(FatFsInfo));
    if(buffer == NULL)
    {
        close (fd);
        return FALSE; 
    }
    if (read(ret, buffer, sizeof(FatFsInfo)) != sizeof(FatFsInfo))
    {
        close (fd);
        free(buffer);
        return FALSE;
    }    
    assert(buffer != NULL);
    memcpy(fatfs_info, buffer, sizeof(FatFsInfo));
    free(buffer);
    
    close (fd);
    return TRUE;
}   
static const char *get_fat_fs_type(FatBootSector *fat_sb)
{
    off_t total_sectors;
    off_t logical_sector_size;
    off_t data_start;
    off_t data_size;
    off_t clusters;
    off_t root_start;
    uint  root_entries;

    if ((fat_sb.u.fat16.ext_signature == 0x29) || 
        (fat_sb.fat_length && !fat_sb.u.fat32.fat_length))
    {
        total_sectors = get_total_sector(fat_sb);
        logical_sector_size = fat_sb.sector_size;
        root_start = (fat_sb.reserved + fat_sb.fats * fat_sb.fat_length) * logical_sector_size;
        root_entries = fat_sb.dir_entries;
        data_start = root_start + ROUND_TO_MULTIPLE(root_entries <<MSDOS_DIR_BITS,logical_sector_size);
        data_size = (off_t) total_sectors * logical_sector_size - data_start;
        if (data_size <= 0)
        {
            return NULL;
        }   
        clusters = data_size / (fat_sb.cluster_size * logical_sector_size);
        if (clusters <= 0)
        {
            return NULL;
        }    
        if (clusters >= FAT12_THRESHOLD)
        {
            FS = FAT_16;
            return "FAT16";

        }
       else
        {
            FS = FAT_12;
            return "FAT12";
        }
    }
    else if ((fat_sb.u.fat32.fat_name[4] == '2')||(!fat_sb.fat_length && fat_sb.u.fat32.fat_length))
    {
        FS = FAT_32;
        return = "FAT32";
    }
    
    return NULL;
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
    FatBootSector fat_sb;
    FatFsInfo fatfs_info;
    ull total_sector = 0;
    ull bused = 0;

    const char *fs_type;

    if (!get_fat_fs_sector (device,&fat_sb))
    {
        return FALSE;
    }     
    if (!get_fat_fs_info (device,&fatfs_info))
    {
        return FALSE;
    }    
    fs_type = get_fat_fs_type (&fat_sb);
    strncpy(fs_info->fs, fs_type, FS_MAGIC_SIZE);
    total_sector = get_total_sector();
    bused = get_used_block();//so I need calculate by myself.
    
    fs_info->block_size  = fat_sb.sector_size;
    fs_info->totalblock  = total_sector;
    fs_info->usedblocks  = bused;
    fs_info->device_size = total_sector * fs_info->block_size;

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
static gboolean read_write_data_ptf (SysbakGdbus      *object,
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
        }
        sysbak_gdbus_emit_sysbak_progress (object,
                                           pdata.percent,
                                           pdata.speed,
                                           pdata.elapsed);
        if (r_size + cs_added * img_opt->checksum_size != w_size)
        {
            goto ERROR;
        }
		//sysbak_gdbus_set_read_size (object,block_id);
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
gboolean gdbus_sysbak_fatfs_ptf (SysbakGdbus           *object,
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
    sysbak_gdbus_complete_sysbak_extfs_ptf (object,invocation); 
    if (!read_write_data_ptf (object,&fs_info,&img_opt,bitmap,&dfr,&dfw))
    {
        e_code = 8;
        goto ERROR;
    } 

    fsync(dfw);
	sysbak_gdbus_emit_sysbak_finished (object,
                                       fs_info.totalblock,
                                       fs_info.usedblocks,
                                       fs_info.block_size);
    free(bitmap);
    close (dfw);
    close (dfr);
    return TRUE;
ERROR:
	sysbak_gdbus_complete_sysbak_extfs_ptf (object,invocation); 
	sysbak_gdbus_emit_sysbak_error (object,
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

static gboolean read_write_data_ptp (SysbakGdbus      *object,
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
        }
		sysbak_gdbus_emit_sysbak_progress (object,
                                           pdata.percent,
                                           pdata.speed,
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
//Backup ext file system partition to partition
gboolean gdbus_sysbak_extfs_ptp (SysbakGdbus           *object,
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
    sysbak_gdbus_complete_sysbak_extfs_ptp (object,invocation); 
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
    sysbak_gdbus_emit_sysbak_finished (object,
                                       fs_info.totalblock,
                                       fs_info.usedblocks,
                                       fs_info.block_size);
    return TRUE;
ERROR:
    sysbak_gdbus_complete_sysbak_extfs_ptp (object,invocation); 
    free(bitmap);
    if (dfr > 0)
    {    
        close (dfr);
    }    
    if (dfw > 0)   
    {    
        close (dfw);
    }    
    sysbak_gdbus_emit_sysbak_error (object,
                                    sysbak_error_message[e_code],
                                    e_code);
    return FALSE;
}  
