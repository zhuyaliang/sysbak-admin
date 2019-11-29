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
#include "gdbus-share.h"
#include <stdbool.h>
#include <assert.h>
#include <sys/types.h>
#include <linux/types.h>
#include <xfs/linux.h>
#include <xfs/xfs_types.h>
#include "gdbus-xfsfs.h"
#include "checksum.h"
#include "gdbus-bitmap.h"
#include "progress.h"
#include <xfs/xfs_format.h>
#include "xfs/libxfs.h"
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
typedef enum typnm
{
    TYP_AGF, TYP_AGFL, TYP_AGI, TYP_ATTR, TYP_BMAPBTA,
    TYP_BMAPBTD, TYP_BNOBT, TYP_CNTBT, TYP_DATA,
    TYP_DIR2, TYP_DQBLK, TYP_INOBT, TYP_INODATA, TYP_INODE,
    TYP_LOG, TYP_RTBITMAP, TYP_RTSUMMARY, TYP_SB, TYP_SYMLINK,
    TYP_TEXT, TYP_NONE
} typnm_t;
static ull copied_count;
static unsigned long long total_block;
static unsigned long* xfs_bitmap;
static int source_fd = -1;
xfs_mount_t     *mp;
xfs_mount_t     mbuf;
libxfs_init_t   xargs;
static void set_bitmap(unsigned long* bitmap, uint64_t start, int count)
{
    uint64_t block;


    for (block = start; block < start+count; block++){
    pc_clear_bit(block, bitmap, total_block);
    }

}

static gboolean get_sb(xfs_sb_t *sbp, xfs_off_t off, int size, xfs_agnumber_t agno)
{
    xfs_dsb_t *buf = NULL;
    int rval = 0;
    
    buf = memalign(libxfs_device_alignment(), size);
    if (buf == NULL) 
    {
        return FALSE;
    }
    memset(buf, 0, size);
    memset(sbp, 0, sizeof(*sbp));

    if (lseek64(source_fd, off, SEEK_SET) != off) 
    {   
        free(buf); 
        buf = NULL;
        return FALSE;
    }

    if (buf && (rval = read(source_fd, buf, size)) != size) 
    {
        free(buf); 
        buf = NULL;
        return FALSE;
    }
    if (buf) 
    {
        //libxfs_sb_from_disk(sbp, buf);
        xfs_sb_from_disk(sbp, buf);
        free(buf); 
        buf = NULL;
    }

    return TRUE;
}

static void fs_close(void)
{
    libxfs_device_close(xargs.ddev);
}
static gboolean fs_open(char* device)
{
    xfs_sb_t        *sb;
    unsigned int    source_blocksize;       /* source filesystem blocksize */
    unsigned int    source_sectorsize;      /* source disk sectorsize */
    if ((source_fd = open(device, O_RDONLY)) < 0) 
    {
        return FALSE;
    }
    memset(&xargs, 0, sizeof(xargs));
    xargs.isdirect = LIBXFS_DIRECT;
    xargs.isreadonly = LIBXFS_ISREADONLY;
    xargs.volname = device;

    if (libxfs_init(&xargs) == 0)
    {
        return FALSE;
    }

    memset(&mbuf, 0, sizeof(xfs_mount_t));
    sb = &mbuf.m_sb;
    if (!get_sb(sb, 0, XFS_MAX_SECTORSIZE, 0))
    {
        return FALSE;
    }    
    mp = libxfs_mount(&mbuf, sb, xargs.ddev, xargs.logdev, xargs.rtdev, 1);
    if (mp == NULL) 
    {
        return FALSE;
    } 
    else if (mp->m_sb.sb_inprogress)  
    {
        return FALSE;
    } 
    else if (mp->m_sb.sb_logstart == 0)  
    {
        return FALSE;
    }
    else if (mp->m_sb.sb_rextents != 0)  
    {
        return FALSE;
    }

    source_blocksize = mp->m_sb.sb_blocksize;
    source_sectorsize = mp->m_sb.sb_sectsize;
    if (source_blocksize < source_sectorsize)  
    {
        return FALSE;
    }
    return TRUE;
}
static void
addtohist(
	xfs_agnumber_t	agno,
	xfs_agblock_t	agbno,
	xfs_extlen_t	len)
{
	unsigned long long start_block;
	
	start_block = (agno*mp->m_sb.sb_agblocks) + agbno;
	set_bitmap(xfs_bitmap, start_block, len);

}


static void
scan_sbtree(
	xfs_agf_t	*agf,
	xfs_agblock_t	root,
	typnm_t		typ,
	int		nlevels,
	void		(*func)(struct xfs_btree_block	*block,
				typnm_t			typ,
				int			level,
				xfs_agf_t		*agf))
{
	xfs_agnumber_t	seqno = be32_to_cpu(agf->agf_seqno);
	struct xfs_buf	*bp;
	int blkbb = 1 << mp->m_blkbb_log;
	void *data;

	bp = libxfs_readbuf(mp->m_ddev_targp, XFS_AGB_TO_DADDR(mp, seqno, root), blkbb, 0, NULL);
        data = bp->b_addr;
	if (data == NULL) {
		return;
	}
	(*func)(data, typ, nlevels - 1, agf);
}


static void
scanfunc_bno(
	struct xfs_btree_block	*block,
	typnm_t			typ,
	int			level,
	xfs_agf_t		*agf)
{
	int			i;
	xfs_alloc_ptr_t		*pp;
	xfs_alloc_rec_t		*rp;

	if (!(be32_to_cpu(block->bb_magic) == XFS_ABTB_MAGIC ||
	      be32_to_cpu(block->bb_magic) == XFS_ABTB_CRC_MAGIC)){
		return;
	}

	if (level == 0) {
		rp = XFS_ALLOC_REC_ADDR(mp, block, 1);
		for (i = 0; i < be16_to_cpu(block->bb_numrecs); i++)
			addtohist(be32_to_cpu(agf->agf_seqno),
					be32_to_cpu(rp[i].ar_startblock),
					be32_to_cpu(rp[i].ar_blockcount));
		return;
	}
	pp = XFS_ALLOC_PTR_ADDR(mp, block, 1, mp->m_alloc_mxr[1]);
	for (i = 0; i < be16_to_cpu(block->bb_numrecs); i++)
		scan_sbtree(agf, be32_to_cpu(pp[i]), typ, level, scanfunc_bno);
}

static void
scan_freelist(
	xfs_agf_t	*agf)
{
	xfs_agnumber_t	seqno = be32_to_cpu(agf->agf_seqno);
	xfs_agfl_t	*agfl;
	xfs_agblock_t	bno;
	unsigned int	i;
	__be32		*agfl_bno;
	struct xfs_buf	*bp;
	const struct xfs_buf_ops *ops = NULL;

	if (be32_to_cpu(agf->agf_flcount) == 0)
		return;
	bp = libxfs_readbuf(mp->m_ddev_targp, XFS_AG_DADDR(mp, seqno, XFS_AGFL_DADDR(mp)), XFS_FSS_TO_BB(mp, 1), 0, ops);
	agfl = bp->b_addr;
	i = be32_to_cpu(agf->agf_flfirst);

	agfl_bno = xfs_sb_version_hascrc(&mp->m_sb) ? &agfl->agfl_bno[0] : (__be32 *)agfl;

	if (be32_to_cpu(agf->agf_flfirst) >= XFS_AGFL_SIZE(mp) ||
	    be32_to_cpu(agf->agf_fllast) >= XFS_AGFL_SIZE(mp)) {
		return;
	}

	for (;;) {
		bno = be32_to_cpu(agfl_bno[i]);
		addtohist(seqno, bno, 1);
		if (i == be32_to_cpu(agf->agf_fllast))
			break;
		if (++i == XFS_AGFL_SIZE(mp))
			i = 0;
	}
}



static void
scan_ag(
	xfs_agnumber_t	agno)
{
	xfs_agf_t	*agf;
	struct xfs_buf	*bp;
	const struct xfs_buf_ops *ops = NULL;

	bp = libxfs_readbuf(mp->m_ddev_targp, XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR(mp)), XFS_FSS_TO_BB(mp, 1), 0, ops);
	agf = bp->b_addr;
	scan_freelist(agf);
	scan_sbtree(agf, be32_to_cpu(agf->agf_roots[XFS_BTNUM_BNO]),
			TYP_BNOBT, be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]),
			scanfunc_bno);
}

static gboolean read_bitmap_info (const char      *device, 
                                  file_system_info fs_info, 
                                  ul              *bitmap) 
{

    xfs_agnumber_t  agno = 0;
    xfs_agnumber_t  num_ags;

    //int start = 0;
    //int bit_size = 1;
    //int bres;
    //pthread_t prog_bitmap_thread;

    uint64_t bused = 0;
    uint64_t bfree = 0;
    unsigned long long current_block = 0;
    total_block = fs_info.totalblock;

    xfs_bitmap = bitmap;

    for(current_block = 0; current_block < fs_info.totalblock; current_block++){
	pc_set_bit(current_block, bitmap, fs_info.totalblock);
    }
    fs_open(device);

    num_ags = mp->m_sb.sb_agcount;

    for (agno = 0; agno < num_ags ; agno++)  {
	scan_ag(agno);
    }
    for(current_block = 0; current_block < fs_info.totalblock; current_block++){
	if(pc_test_bit(current_block, bitmap, fs_info.totalblock))
	    bused++;
	else
	    bfree++;
    }
    fs_close();
    return TRUE;
}

static gboolean read_super_blocks(const char* device, file_system_info* fs_info)
{
    if (!fs_open(device))
    {
        return FALSE;
    }    
    strncpy(fs_info->fs, xfs_MAGIC, FS_MAGIC_SIZE);
    fs_info->block_size  = mp->m_sb.sb_blocksize;
    fs_info->totalblock  = mp->m_sb.sb_dblocks;
    fs_info->usedblocks  = mp->m_sb.sb_dblocks - mp->m_sb.sb_fdblocks;
    fs_info->device_size = fs_info->totalblock * fs_info->block_size;
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
        fsync(*dfw);
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
gboolean gdbus_sysbak_xfsfs_ptf (SysbakGdbus           *object,
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
    sysbak_gdbus_complete_sysbak_xfsfs_ptf (object,invocation); 
    if (!read_write_data_ptf (object,&fs_info,&img_opt,bitmap,&dfr,&dfw))
    {
        e_code = 8;
        goto ERROR;
    } 

	sysbak_gdbus_emit_sysbak_finished (object,
                                       fs_info.totalblock,
                                       fs_info.usedblocks,
                                       fs_info.block_size);
    free(bitmap);
    close (dfw);
    close (dfr);
    return TRUE;
ERROR:
	sysbak_gdbus_complete_sysbak_xfsfs_ptf (object,invocation); 
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
gboolean gdbus_sysbak_xfsfs_ptp (SysbakGdbus           *object,
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
    sysbak_gdbus_complete_sysbak_xfsfs_ptp (object,invocation); 
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
    sysbak_gdbus_complete_sysbak_xfsfs_ptp (object,invocation); 
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
