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
#include <assert.h>
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
static const char *sysbak_error_message[10] = 
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
///return sec_per_fat
static ull get_sec_per_fat(FatBootSector *fat_sb)
{
    ull sec_per_fat = 0;
    /// get fat length
    if(fat_sb->fat_length != 0)
        sec_per_fat = fat_sb->fat_length;
    else
        if (fat_sb->u.fat32.fat_length != 0)
        sec_per_fat = fat_sb->u.fat32.fat_length;
    return sec_per_fat;

}

///return root sec
static ull get_root_sec(FatBootSector *fat_sb)
{
    ull root_sec = 0;
    root_sec = ((fat_sb->dir_entries * 32) + fat_sb->sector_size - 1) / fat_sb->sector_size;
    return root_sec;
}

static ull get_cluster_count(FatBootSector *fat_sb)
{
    ull data_sec = 0;
    ull cluster_count = 0;
    ull total_sector = get_total_sector(fat_sb);
    ull root_sec = get_root_sec(fat_sb);
    ull sec_per_fat = get_sec_per_fat(fat_sb);
    ull reserved = fat_sb->reserved +
             (fat_sb->fats * sec_per_fat) + root_sec;

    if (reserved > total_sector) {
        return 0;
    }

    data_sec = total_sector - ( fat_sb->reserved + (fat_sb->fats * sec_per_fat) + root_sec);
    cluster_count = data_sec / fat_sb->cluster_size;
    return cluster_count;
}
/// mark reserved sectors as used
static ull mark_reserved_sectors(ul *fat_bitmap, ull block,FatBootSector *fat_sb)
{
    ull i = 0;
    ull j = 0;
    ull sec_per_fat = 0;
    ull root_sec = 0;
    ull total_block;
    
    total_block = get_total_sector (fat_sb);
    sec_per_fat = get_sec_per_fat(fat_sb);
    root_sec = get_root_sec(fat_sb);

    /// A) the reserved sectors are used
    for (i=0; i < fat_sb->reserved; i++,block++)
        pc_set_bit(block, fat_bitmap, total_block);

    /// B) the FAT tables are on used sectors
    for (j=0; j < fat_sb->fats; j++)
        for (i=0; i < sec_per_fat ; i++,block++)
            pc_set_bit(block, fat_bitmap, total_block);

    /// C) The rootdirectory is on used sectors
    if (root_sec > 0) /// no rootdir sectors on FAT32
        for (i=0; i < root_sec; i++,block++)
            pc_set_bit(block, fat_bitmap, total_block);
    return block;
}
static int check_fat_status(int fd) 
{
    uint16_t Fat16_Entry;
    uint32_t Fat32_Entry;
    int fs_error = 2;
    int fs_good = 0;
    int fs_bad = 1;

    if (FS == FAT_16)
    {
        read(fd, &Fat16_Entry, sizeof(Fat16_Entry));
        if (!(Fat16_Entry & 0x8000))
            return fs_bad;
        if (!(Fat16_Entry & 0x4000))
            return fs_error;

    } 
    else if (FS == FAT_32) 
    {
        read(fd, &Fat32_Entry, sizeof(Fat32_Entry));
        if (!(Fat32_Entry & 0x08000000))
            return fs_bad;
        if (!(Fat32_Entry & 0x04000000))
            return fs_error;
    } 
    return fs_good;
}
static ull check_fat16_entry(ul  *fat_bitmap, 
                             ull  total_block,
                             ull  cluster_size,
                             ull  block, 
                             ull *bfree, 
                             ull *bused, 
                             ull *DamagedClusters,
                             int  fd)
{
    uint16_t Fat16_Entry = 0;
    ull i = 0;
    
    read(fd, &Fat16_Entry, sizeof(Fat16_Entry));
    if (Fat16_Entry  == 0xFFF7) 
    { /// bad FAT16 cluster
        DamagedClusters++;
        for (i=0; i < cluster_size; i++,block++)
            pc_clear_bit(block, fat_bitmap, total_block);
    } 
    else if (Fat16_Entry == 0x0000)
    { /// free
        bfree++;
        for (i=0; i < cluster_size; i++,block++)
            pc_clear_bit(block, fat_bitmap, total_block);
    } 
    else 
    {
        bused++;
        for (i=0; i < cluster_size; i++,block++)
            pc_set_bit(block, fat_bitmap, total_block);
    }
    return block;
}
/// check per FAT32 entry
static ull check_fat32_entry(ul  *fat_bitmap,
                             ull  total_block,
                             ull  cluster_size,
                             ull  block, 
                             ull *bfree, 
                             ull *bused, 
                             ull *DamagedClusters,
                             int  fd)
{
    uint32_t Fat32_Entry = 0;
    ull i = 0;
    
    read(fd, &Fat32_Entry, sizeof(Fat32_Entry));
    if (Fat32_Entry  == 0x0FFFFFF7) 
    { /// bad FAT32 cluster
        DamagedClusters++;
        for (i=0; i < cluster_size; i++,block++)
            pc_clear_bit(block, fat_bitmap, total_block);
    } 
    else if (Fat32_Entry == 0x0000)
    { /// free
        bfree++;
        for (i=0; i < cluster_size; i++,block++)
            pc_clear_bit(block, fat_bitmap, total_block);
    } 
    else 
    {
        bused++;
        for (i=0; i < cluster_size; i++,block++)
            pc_set_bit(block, fat_bitmap, total_block);
    }

    return block;
}
/// check per FAT12 entry
static ull check_fat12_entry(ul  *fat_bitmap, 
                             ull  total_block,
                             ull  cluster_size,
                             ull  block, 
                             ull *bfree, 
                             ull *bused, 
                             ull *DamagedClusters,
                             int  fd)
{
    uint16_t Fat16_Entry = 0;
    uint16_t Fat12_Entry = 0;
    ull i = 0;
    
    read(fd, &Fat16_Entry, sizeof(Fat16_Entry));
    Fat12_Entry = Fat16_Entry>>4;
    if (Fat12_Entry  == 0xFF7) 
    { /// bad FAT12 cluster
        DamagedClusters++;
        for (i=0; i < cluster_size; i++,block++)
            pc_clear_bit(block, fat_bitmap, total_block);
    } 
    else if (Fat12_Entry == 0x0000)
    { /// free
        bfree++;
        for (i=0; i < cluster_size; i++,block++)
            pc_clear_bit(block, fat_bitmap, total_block);
    } 
    else 
    {
        bused++;
        for (i=0; i < cluster_size; i++,block++)
            pc_set_bit(block, fat_bitmap, total_block);
    }
    return block;
}
/// get_used_block - get FAT used blocks
static ull get_used_block(FatBootSector *fat_sb,int fd)
{
    ull i = 0;
    int fat_stat = 0;
    ull block = 0, bfree = 0, bused = 0, DamagedClusters = 0;
    ull cluster_count = 0, total_sector = 0;
    ull real_back_block= 0;
    int FatReservedBytes = 0;
    unsigned long *fat_bitmap;

    total_sector = get_total_sector(fat_sb);
    cluster_count = get_cluster_count(fat_sb);
    fat_bitmap = pc_alloc_bitmap(total_sector);
    pc_init_bitmap(fat_bitmap, 0xFF, total_sector);

    /// A) B) C)
    block = mark_reserved_sectors(fat_bitmap, block,fat_sb);

    /// D) The clusters
    FatReservedBytes = fat_sb->sector_size * fat_sb->reserved;

    lseek(fd, FatReservedBytes, SEEK_SET);

    /// The second fat is used to check FAT status
    fat_stat = check_fat_status(fd);
    if (fat_stat != 0)
    {
        return 0;
    }
    for (i=0; i < cluster_count; i++)
    {
        if(FS == FAT_16)
        {
            block = check_fat16_entry(fat_bitmap,
                                      total_sector, 
                                      fat_sb->cluster_size,
                                      block, 
                                      &bfree, 
                                      &bused, 
                                      &DamagedClusters,
                                      fd);
        } 
        else if (FS == FAT_32)
        { /// FAT32
            block = check_fat32_entry(fat_bitmap,
                                      total_sector, 
                                      fat_sb->cluster_size,
                                      block, 
                                      &bfree, 
                                      &bused, 
                                      &DamagedClusters,
                                      fd);
        } 
        else if (FS == FAT_12)
        { /// FAT12
            block = check_fat12_entry(fat_bitmap,
                                      total_sector, 
                                      fat_sb->cluster_size,
                                      block, 
                                      &bfree, 
                                      &bused, 
                                      &DamagedClusters,
                                      fd);
        } 
    }

    while(block < total_sector){
        pc_set_bit(block, fat_bitmap, total_sector);
        block++;
    }
    for (block = 0; block < total_sector; block++)
    {
        if (pc_test_bit(block, fat_bitmap, total_sector)) {
            real_back_block++;
        }
    }
    free(fat_bitmap);

    return real_back_block;
}

// open device
static int get_fat_fs_sector (const char* device,FatBootSector *fat_sb)
{
    char *buffer;
    int fd;

    fd = open(device, O_RDONLY);
    if (fd < 0 )
    {
        return -1;
    }     
    buffer = (char*)malloc(sizeof(FatBootSector));
    if(buffer == NULL)
    {
        return -1; 
    }
    if(read (fd, buffer, sizeof(FatBootSector)) != sizeof(FatBootSector))
    {
        free(buffer);
        return -1;
    }    
    assert(buffer != NULL);
    memcpy(fat_sb, buffer, sizeof(FatBootSector));
    free(buffer);
    
    return fd;
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

    if ((fat_sb->u.fat16.ext_signature == 0x29) || 
        (fat_sb->fat_length && !fat_sb->u.fat32.fat_length))
    {
        total_sectors = get_total_sector(fat_sb);
        logical_sector_size = fat_sb->sector_size;
        root_start = (fat_sb->reserved + fat_sb->fats * fat_sb->fat_length) * logical_sector_size;
        root_entries = fat_sb->dir_entries;
        data_start = root_start + ROUND_TO_MULTIPLE(root_entries <<MSDOS_DIR_BITS,logical_sector_size);
        data_size = (off_t) total_sectors * logical_sector_size - data_start;
        if (data_size <= 0)
        {
            return NULL;
        }   
        clusters = data_size / (fat_sb->cluster_size * logical_sector_size);
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
    else if ((fat_sb->u.fat32.fat_name[4] == '2')||(!fat_sb->fat_length && fat_sb->u.fat32.fat_length))
    {
        FS = FAT_32;
        return  "FAT32";
    }
    
    return NULL;
}    
// reference dumpe2fs
static gboolean read_bitmap_info (const char      *device, 
                                  file_system_info fs_info, 
                                  ul              *bitmap) 
{
    ull i = 0;
    int fat_stat = 0;
    FatBootSector fat_sb;
    ull block = 0, bfree = 0, bused = 0, DamagedClusters = 0;
    ull cluster_count = 0;
    ull total_sector = 0;
    ull FatReservedBytes = 0;
    int fd;

    fd = get_fat_fs_sector (device,&fat_sb);
    if (fd < 0)
    {
        return FALSE;
    }     

    total_sector = get_total_sector(&fat_sb);
    cluster_count = get_cluster_count(&fat_sb);

    pc_init_bitmap(bitmap, 0xFF, total_sector);

    /// A) B) C)
    block = mark_reserved_sectors(bitmap, block,&fat_sb);

    /// D) The clusters
    FatReservedBytes = fat_sb.sector_size * fat_sb.reserved;

    lseek(fd, FatReservedBytes, SEEK_SET);

    /// The second used to check FAT status
    fat_stat = check_fat_status(fd);
    if (fat_stat != 0)
    {
        close (fd);
        return FALSE;
    }    
    for (i=0; i < cluster_count; i++)
    {
        if(FS == FAT_16)
        {
            block = check_fat16_entry(bitmap, 
                                      total_sector,
                                      fat_sb.cluster_size,
                                      block, 
                                      &bfree, 
                                      &bused, 
                                      &DamagedClusters,
                                      fd);
        } 
        else if (FS == FAT_32)
        { /// FAT32
            block = check_fat32_entry(bitmap, 
                                      total_sector,
                                      fat_sb.cluster_size,
                                      block, 
                                      &bfree, 
                                      &bused, 
                                      &DamagedClusters,
                                      fd);
        } 
        else if (FS == FAT_12)
        { /// FAT12
            block = check_fat12_entry(bitmap,
                                      total_sector,
                                      fat_sb.cluster_size, 
                                      block, 
                                      &bfree, 
                                      &bused, 
                                      &DamagedClusters,
                                      fd);
        } 
    }
    close(fd);

    return TRUE;
}
static gboolean read_super_blocks(const char* device, file_system_info* fs_info)
{
    FatBootSector fat_sb;
    ull total_sector = 0;
    ull bused = 0;
    int fd;

    const char *fs_type;

    fd = get_fat_fs_sector (device,&fat_sb);
    if (fd < 0)
    {
        return FALSE;
    }     
    fs_type = get_fat_fs_type (&fat_sb);
    strncpy(fs_info->fs, fs_type, FS_MAGIC_SIZE);
    total_sector = get_total_sector(&fat_sb);
    bused = get_used_block(&fat_sb,fd);//so I need calculate by myself.
    
    fs_info->block_size  = fat_sb.sector_size;
    fs_info->totalblock  = total_sector;
    fs_info->usedblocks  = bused;
    fs_info->device_size = total_sector * fs_info->block_size;
    
    close (fd);
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
    sysbak_gdbus_complete_sysbak_fatfs_ptf (object,invocation); 
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
	sysbak_gdbus_complete_sysbak_fatfs_ptf (object,invocation); 
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
gboolean gdbus_sysbak_fatfs_ptp (SysbakGdbus           *object,
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
    sysbak_gdbus_complete_sysbak_fatfs_ptp (object,invocation); 
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
    sysbak_gdbus_complete_sysbak_fatfs_ptp (object,invocation); 
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
