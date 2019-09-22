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
#include <btrfs/radix-tree.h>
#include "btrfs/ctree.h"
#include "gdbus-btrfs.h"
#include "gdbus-share.h"
#include "checksum.h"
#include "gdbus-bitmap.h"
#include "progress.h"
#include "btrfs/volumes.h"
#include "btrfs/disk-io.h"
#include "btrfs/utils.h"
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
struct btrfs_fs_info *info;
struct btrfs_root *root;
struct btrfs_path path;
int block_size = 0;
uint64_t dev_size = 0;
unsigned long long total_block = 0;

///set useb block
static void set_bitmap(unsigned long* bitmap, uint64_t pos, uint64_t length){
    uint64_t block;
    uint64_t pos_block;
    uint64_t block_end;

    if (pos > dev_size) {
	return;
    }
    pos_block = pos/block_size;
    block_end = (pos+length)/block_size;
    if ((pos+length)%block_size > 0)
	block_end++;

    for(block = pos_block; block < block_end; block++){
	pc_set_bit(block, bitmap, total_block);
    }
}

int check_extent_bitmap(unsigned long* bitmap, u64 bytenr, u64 *num_bytes, int type)
{
    struct btrfs_multi_bio *multi = NULL;
    int ret = 0;
    int mirror = 0;
    u64 maxlen;
    
    if (*num_bytes % root->sectorsize)
	return -EINVAL;

    if (type == 1){
	maxlen = *num_bytes;
    }

    btrfs_map_block(&info->mapping_tree, READ, bytenr, num_bytes,
	    &multi, mirror, NULL);

    if (type == 1){
        *num_bytes = maxlen;
    }

    set_bitmap(bitmap, multi->stripes[0].physical, *num_bytes);
    return 0;
}

static void dump_file_extent_item(unsigned long* bitmap, struct extent_buffer *eb,
				   struct btrfs_item *item,
				   int slot,
				   struct btrfs_file_extent_item *fi)
{
	int extent_type = btrfs_file_extent_type(eb, fi);

	if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
	    return;
	}
	set_bitmap(bitmap, (unsigned long long)btrfs_file_extent_disk_bytenr(eb, fi),
		                   (unsigned long long)btrfs_file_extent_disk_num_bytes(eb, fi) );
}
void dump_start_leaf(unsigned long* bitmap, struct btrfs_root *root, struct extent_buffer *eb, int follow){

    u64 bytenr;
    u64 size;
    u64 objectid;
    u64 offset;
    u64 parent_transid;
    u32 type;
    int i;
    struct btrfs_item *item;
    struct btrfs_disk_key disk_key;
    struct btrfs_file_extent_item *fi;
    struct extent_buffer *next;
    u32 leaf_size;

    if (!eb)
	return;
    u32 nr = btrfs_header_nritems(eb);
    if (btrfs_is_leaf(eb)) 
    {
	    size = (u64)root->nodesize;
	    bytenr = (unsigned long long)btrfs_header_bytenr(eb);
	    check_extent_bitmap(bitmap, bytenr, &size, 0);
        for (i = 0 ; i < nr ; i++) 
        {
	        item = btrfs_item_nr(i);
	        btrfs_item_key(eb, &disk_key, i);
	        type = btrfs_disk_key_type(&disk_key);
	        if (type == BTRFS_EXTENT_DATA_KEY)
            {
		        fi = btrfs_item_ptr(eb, i,
			                        struct btrfs_file_extent_item);
		        dump_file_extent_item(bitmap, eb, item, i, fi);
	        }
	        if (type == BTRFS_EXTENT_ITEM_KEY)
            {
		        objectid = btrfs_disk_key_objectid(&disk_key);
		        offset = btrfs_disk_key_offset(&disk_key);
		        check_extent_bitmap(bitmap, objectid, &offset, 1);
	        }
	    }
	    return;
    }
    if (!follow)
	    return;
    leaf_size = root->nodesize;
    for (i = 0; i < nr; i++) 
    {
	    bytenr = (unsigned long long)btrfs_header_bytenr(eb);
	    check_extent_bitmap(bitmap, bytenr, &size, 0);
	    bytenr = btrfs_node_blockptr(eb, i);
        parent_transid = btrfs_node_ptr_generation(eb, i);
        next = read_tree_block(root,bytenr,leaf_size,parent_transid);
	    bytenr = (unsigned long long)btrfs_header_bytenr(next);
	    check_extent_bitmap(bitmap, bytenr, &size, 0);
	    if (!extent_buffer_uptodate(next)) 
        {
	        continue;
	    }
        if (btrfs_is_leaf(next) && btrfs_header_level(eb) != 1)
            g_print("%s(%i): BUG\r\n", __FILE__, __LINE__);
        if (btrfs_header_level(next) != btrfs_header_level(eb) - 1)
            g_print("%s(%i): BUG\r\n", __FILE__, __LINE__);

	    dump_start_leaf(bitmap, root, next, 1);
	    free_extent_buffer(next);
    }

}

/// open device
static gboolean fs_open(char* device)
{
    struct cache_tree root_cache;
    u64 bytenr = 0;
    enum btrfs_open_ctree_flags ctree_flags = OPEN_CTREE_PARTIAL;

    radix_tree_init();
    cache_tree_init(&root_cache);
    info = open_ctree_fs_info(device, bytenr, 0, 0, ctree_flags);
    root = info->fs_root;

    if (!info || !root) 
    {
	    return FALSE;
    }

    if (!extent_buffer_uptodate(info->tree_root->node) ||
	    !extent_buffer_uptodate(info->dev_root->node) ||
	    !extent_buffer_uptodate(info->chunk_root->node)) 
    {
	    return FALSE;
    }

    return TRUE;
}

/// close device
static void fs_close(){
    close_ctree(root);
}
static gboolean read_bitmap_info (char* device, file_system_info fs_info, ul *bitmap)
{

    int ret;
    struct btrfs_root *tree_root_scan;
    struct btrfs_key key;
    struct btrfs_disk_key disk_key;
    struct btrfs_key found_key;
    struct extent_buffer *leaf;
    struct btrfs_root_item ri;
    int slot;

    total_block = fs_info.totalblock;
    if (!fs_open(device))
    {
        return FALSE;
    }    
    dev_size = fs_info.device_size;
    block_size  = btrfs_super_nodesize(info->super_copy);
    u64 bsize = (u64)block_size;
    set_bitmap(bitmap, 0, BTRFS_SUPER_INFO_OFFSET); // some data like mbr maybe in
    set_bitmap(bitmap, BTRFS_SUPER_INFO_OFFSET, block_size);
    check_extent_bitmap(bitmap, btrfs_root_bytenr(&info->extent_root->root_item), &bsize, 0);
    check_extent_bitmap(bitmap, btrfs_root_bytenr(&info->csum_root->root_item), &bsize, 0);
    check_extent_bitmap(bitmap, btrfs_root_bytenr(&info->dev_root->root_item), &bsize, 0);
    check_extent_bitmap(bitmap, btrfs_root_bytenr(&info->fs_root->root_item), &bsize, 0);

    if (info->tree_root->node) 
    {
	    dump_start_leaf(bitmap, info->tree_root, info->tree_root->node, 1);
    }
    if (info->chunk_root->node) 
    {
	    dump_start_leaf(bitmap, info->chunk_root, info->chunk_root->node, 1);
    }
    tree_root_scan = info->tree_root;
    btrfs_init_path(&path);
    if (!extent_buffer_uptodate(tree_root_scan->node))
	goto no_node;
    key.offset = 0;
    key.objectid = 0;
    btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);
    ret = btrfs_search_slot(NULL, tree_root_scan, &key, &path, 0, 0);
    while(1) {

	leaf = path.nodes[0];
	slot = path.slots[0];
	if (slot >= btrfs_header_nritems(leaf)) {
	    ret = btrfs_next_leaf(tree_root_scan, &path);
	    if (ret != 0)
		break;
	    leaf = path.nodes[0];
	    slot = path.slots[0];
	}
	btrfs_item_key(leaf, &disk_key, path.slots[0]);
	btrfs_disk_key_to_cpu(&found_key, &disk_key);
	if (btrfs_key_type(&found_key) == BTRFS_ROOT_ITEM_KEY)
    {
	    unsigned long offset;
	    struct extent_buffer *buf;

	    offset = btrfs_item_ptr_offset(leaf, slot);
	    read_extent_buffer(leaf, &ri, offset, sizeof(ri));
	    buf = read_tree_block(tree_root_scan,
		    btrfs_root_bytenr(&ri),
		    root->nodesize,
		    0);
	    if (!extent_buffer_uptodate(buf))
		    goto next;
	    dump_start_leaf(bitmap, tree_root_scan, buf, 1);
	    free_extent_buffer(buf);
	}
next:
	path.slots[0]++;
    }
no_node:
    btrfs_release_path(&path);
    return TRUE;
}

static gboolean read_super_blocks(const char* device, file_system_info* fs_info)
{
    if (!fs_open(device))
    {
        return FALSE;
    }    
    strncpy(fs_info->fs, btrfs_MAGIC, FS_MAGIC_SIZE);
    fs_info->block_size  = btrfs_super_nodesize(root->fs_info->super_copy);
    fs_info->usedblocks  = btrfs_super_bytes_used(root->fs_info->super_copy) / fs_info->block_size;
    fs_info->device_size = btrfs_super_total_bytes(root->fs_info->super_copy);
    fs_info->totalblock  = fs_info->device_size / fs_info->block_size;
    fs_close();

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
gboolean gdbus_sysbak_btrfs_ptf (SysbakGdbus           *object,
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
    sysbak_gdbus_complete_sysbak_btrfs_ptf (object,invocation); 
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
	sysbak_gdbus_complete_sysbak_btrfs_ptf (object,invocation); 
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
gboolean gdbus_sysbak_btrfs_ptp (SysbakGdbus           *object,
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
    sysbak_gdbus_complete_sysbak_btrfs_ptp (object,invocation); 
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
    sysbak_gdbus_complete_sysbak_btrfs_ptp (object,invocation); 
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
