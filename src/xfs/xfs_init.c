/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <sys/stat.h>
#include "xfs_init.h"

#include "libxfs_priv.h"
#include "xfs_fs.h"
//#include "xfs_shared.h"
#include <xfs/xfs_format.h>
#include <xfs/xfs_log_format.h>
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "radix-tree.h"
#include "xfs_defer.h"
#include "xfs_inode_buf.h"
#include "xfs_inode_fork.h"
#include "xfs_inode.h"
//#include "xfs_rmap_btree.h"
#include "libxfs_priv.h"
#include "libxfs.h"		/* for now */

#define XFS_BMDR_SPACE_CALC(nrecs) \
	(int)(sizeof(xfs_bmdr_block_t) + \
	       ((nrecs) * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t))))
#define	XFS_INODE_BIG_CLUSTER_SIZE	8192
#define KM_SLEEP	0x0001u
#define KM_NOSLEEP	0x0002u
#define KM_NOFS		0x0004u
#define KM_MAYFAIL	0x0008u
#define KM_LARGE	0x0010u
struct cache *libxfs_bcache;	/* global buffer cache */
int libxfs_bhash_size;		/* #buckets in bcache */

int	use_xfs_buf_lock;	/* global flag: use xfs_buf_t locks for MT */

static void manage_zones(int);	/* setup global zones */

kmem_zone_t	*xfs_inode_zone;

struct xfs_log_item_desc {
	struct xfs_log_item	*lid_item;
	struct list_head	lid_trans;
	unsigned char		lid_flags;
};
/*
 * dev_map - map open devices to fd.
 */
#define MAX_DEVS 10	/* arbitary maximum */
int nextfakedev = -1;	/* device number to give to next fake device */
static struct dev_to_fd {
	dev_t	dev;
	int	fd;
} dev_map[MAX_DEVS]={{0}};

/*
 * Checks whether a given device has a mounted, writable
 * filesystem, returns 1 if it does & fatal (just warns
 * if not fatal, but allows us to proceed).
 *
 * Useful to tools which will produce uncertain results
 * if the filesystem is active - repair, check, logprint.
 */
static int
check_isactive(char *name, char *block, int fatal)
{
	struct stat	st;

	if (stat(block, &st) < 0)
		return 0;
	if ((st.st_mode & S_IFMT) != S_IFBLK)
		return 0;
	if (platform_check_ismounted(name, block, &st, 0) == 0)
		return 0;
	if (platform_check_iswritable(name, block, &st))
		return fatal ? 1 : 0;
	return 0;
}

int
libxfs_device_to_fd(dev_t device)
{
	int	d;
	for (d = 0; d < MAX_DEVS; d++)
    {    
        if (dev_map[d].dev == device)
			return dev_map[d].fd;
    }    
	exit(1);
}

/* libxfs_device_open:
 *     open a device and return its device number
 */
dev_t
libxfs_device_open(char *path, int creat, int xflags, int setblksize)
{
	dev_t		dev;
	int		fd, d, flags;
	int		readonly, dio, excl;
	struct stat	statb;

	readonly = (xflags & LIBXFS_ISREADONLY);
	excl = (xflags & LIBXFS_EXCLUSIVELY) && !creat;
	dio = (xflags & LIBXFS_DIRECT) && !creat && platform_direct_blockdev();

retry:
	flags = (readonly ? O_RDONLY : O_RDWR) | \
		(creat ? (O_CREAT|O_TRUNC) : 0) | \
		(dio ? O_DIRECT : 0) | \
		(excl ? O_EXCL : 0);

	if ((fd = open(path, flags, 0666)) < 0) {
		if (errno == EINVAL && --dio == 0)
			goto retry;
		exit(1);
	}

	if (fstat(fd, &statb) < 0) {
		exit(1);
	}
	if (!readonly && setblksize && (statb.st_mode & S_IFMT) == S_IFBLK) {
		if (setblksize == 1)
			/* use the default blocksize */
			(void)platform_set_blocksize(fd, path, statb.st_rdev, XFS_MIN_SECTORSIZE, 0);
		else {
			/* given an explicit blocksize to use */
			if (platform_set_blocksize(fd, path, statb.st_rdev, setblksize, 1))
			    exit(1);
		}
	}

	dev = (statb.st_rdev) ? (statb.st_rdev) : (nextfakedev--);

	for (d = 0; d < MAX_DEVS; d++)
		if (dev_map[d].dev == dev) 
        {
			exit(1);
		}

	for (d = 0; d < MAX_DEVS; d++)
		if (!dev_map[d].dev) 
        {
			dev_map[d].dev = dev;
			dev_map[d].fd = fd;

			return dev;
		}
	exit(1);
	/* NOTREACHED */
}

void
libxfs_device_close(dev_t dev)
{
	int	d;

	for (d = 0; d < MAX_DEVS; d++)
		if (dev_map[d].dev == dev) {
			int	fd;

			fd = dev_map[d].fd;
			dev_map[d].dev = dev_map[d].fd = 0;

			fsync(fd);
			platform_flush_device(fd, dev);
			close(fd);

			return;
		}

	exit(1);
}

static int
check_open(char *path, int flags, char **rawfile, char **blockfile)
{
	int readonly = (flags & LIBXFS_ISREADONLY);
	int inactive = (flags & LIBXFS_ISINACTIVE);
	int dangerously = (flags & LIBXFS_DANGEROUSLY);
	struct stat	stbuf;

	if (stat(path, &stbuf) < 0) {
		perror(path);
		return 0;
	}
	if (!(*rawfile = platform_findrawpath(path))) {
		return 0;
	}
	if (!(*blockfile = platform_findblockpath(path))) {
		return 0;
	}
	if (!readonly && !inactive && platform_check_ismounted(path, *blockfile, NULL, 1))
		return 0;

	if (inactive && check_isactive(path, *blockfile, ((readonly|dangerously)?1:0)))
		return 0;

	return 1;
}

/*
 * libxfs initialization.
 * Caller gets a 0 on failure (and we print a message), 1 on success.
 */
int
libxfs_init(libxfs_init_t *a)
{
	char		*blockfile;
	char		curdir[MAXPATHLEN];
	char		*dname;
	int		fd;
	int		needcd;
	char		*rawfile;
	char		*rtname;
	int		rval = 0;
	int		flags;

	dname = a->dname;
	a->dfd = -1;
	a->ddev = 0;
	a->dsize = 0;

	(void)getcwd(curdir,MAXPATHLEN);
	needcd = 0;
	fd = -1;
	flags = (a->isreadonly | a->isdirect);

	xfs_extent_free_init_defer_op();
	xfs_rmap_update_init_defer_op();
	xfs_refcount_update_init_defer_op();
	xfs_bmap_update_init_defer_op();

	radix_tree_init();

	if (a->volname) 
    {
		if(!check_open(a->volname,flags,&rawfile,&blockfile))
			goto done;
		needcd = 1;
		fd = open(rawfile, O_RDONLY);
		dname = a->dname = a->volname;
		a->volname = NULL;
	}
	if (dname) 
    {
		if (dname[0] != '/' && needcd)
			chdir(curdir);
		if (a->disfile) 
        {
			a->ddev= libxfs_device_open(dname, a->dcreat, flags,
						    a->setblksize);
			a->dfd = libxfs_device_to_fd(a->ddev);
			platform_findsizes(dname, a->dfd, &a->dsize,
					   &a->dbsize);
		} 
        else 
        {
			if (!check_open(dname, flags, &rawfile, &blockfile))
				goto done;
			a->ddev = libxfs_device_open(rawfile,
					a->dcreat, flags, a->setblksize);
			a->dfd = libxfs_device_to_fd(a->ddev);
			platform_findsizes(rawfile, a->dfd,
					   &a->dsize, &a->dbsize);
		}
		needcd = 1;
	} 
    else
		a->dsize = 0;
	if (a->dsize < 0) {
		goto done;
	}
	if (needcd)
		chdir(curdir);
	if (!libxfs_bhash_size)
		libxfs_bhash_size = LIBXFS_BHASHSIZE(sbp);
    
	libxfs_bcache = cache_init(a->bcache_flags, libxfs_bhash_size,
				   &libxfs_bcache_operations);
                 
	use_xfs_buf_lock = a->usebuflock;
	manage_zones(0);
	rval = 1;
done:
	if (fd >= 0)
		close(fd);
	if (!rval && a->ddev)
		libxfs_device_close(a->ddev);
	return rval;
}

static kmem_zone_t *
kmem_zone_init(int size, char *name)
{
	kmem_zone_t	*ptr = malloc(sizeof(kmem_zone_t));

	if (ptr == NULL) {
		exit(1);
	}
	ptr->zone_unitsize = size;
	ptr->zone_name = name;
	ptr->allocated = 0;
	return ptr;
}
kmem_zone_t *xfs_da_state_zone;	/* anchor for state struct zone */
kmem_zone_t	*xfs_btree_cur_zone;
kmem_zone_t		*xfs_bmap_free_item_zone;
kmem_zone_t	*xfs_log_item_desc_zone;
static void
manage_zones(int release)
{
	extern kmem_zone_t	*xfs_buf_zone;
	extern kmem_zone_t	*xfs_ifork_zone;
	extern kmem_zone_t	*xfs_da_state_zone;
	extern kmem_zone_t	*xfs_btree_cur_zone;
	extern kmem_zone_t	*xfs_bmap_free_item_zone;
	extern kmem_zone_t	*xfs_log_item_desc_zone;
	//extern void		xfs_dir_startup();

	if (release) {	/* free zone allocation */
		free(xfs_buf_zone);
		free(xfs_inode_zone);
		free(xfs_da_state_zone);
		free(xfs_btree_cur_zone);
		free(xfs_bmap_free_item_zone);
		free(xfs_log_item_desc_zone);
		return;
	}
	/* otherwise initialise zone allocation */
	xfs_buf_zone = kmem_zone_init(sizeof(xfs_buf_t), "xfs_buffer");
	xfs_inode_zone = kmem_zone_init(sizeof(struct xfs_inode), "xfs_inode");
	xfs_da_state_zone = kmem_zone_init(
			sizeof(xfs_da_state_t), "xfs_da_state");
	xfs_btree_cur_zone = kmem_zone_init(
			sizeof(xfs_btree_cur_t), "xfs_btree_cur");
	xfs_bmap_free_item_zone = kmem_zone_init(
			sizeof(struct xfs_extent_free_item),
			"xfs_bmap_free_item");
	xfs_log_item_desc_zone = kmem_zone_init(
			sizeof(struct xfs_log_item_desc), "xfs_log_item_desc");
	//xfs_dir_startup();
}

/*
 * Initialize realtime fields in the mount structure.
 */
static int
rtmount_init(
	xfs_mount_t	*mp,	/* file system mount structure */
	int		flags)
{
	xfs_buf_t	*bp;	/* buffer for last block of subvolume */
	xfs_daddr_t	d;	/* address of last block of subvolume */
	xfs_sb_t	*sbp;	/* filesystem superblock copy in mount */

	sbp = &mp->m_sb;
	if (sbp->sb_rblocks == 0)
		return 0;
	if (mp->m_rtdev_targp->dev == 0 && !(flags & LIBXFS_MOUNT_DEBUGGER)) {
		return -1;
	}
	mp->m_rsumlevels = sbp->sb_rextslog + 1;
	mp->m_rsumsize =
		(uint)sizeof(xfs_suminfo_t) * mp->m_rsumlevels *
		sbp->sb_rbmblocks;
	mp->m_rsumsize = roundup(mp->m_rsumsize, sbp->sb_blocksize);
	mp->m_rbmip = mp->m_rsumip = NULL;

	/*
	 * Allow debugger to be run without the realtime device present.
	 */
	if (flags & LIBXFS_MOUNT_DEBUGGER)
		return 0;

	/*
	 * Check that the realtime section is an ok size.
	 */
	d = (xfs_daddr_t)XFS_FSB_TO_BB(mp, mp->m_sb.sb_rblocks);
	if (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_rblocks) {
		return -1;
	}
	bp = libxfs_readbuf(mp->m_rtdev,
			d - XFS_FSB_TO_BB(mp, 1), XFS_FSB_TO_BB(mp, 1), 0, NULL);
	if (bp == NULL) {
		return -1;
	}
	libxfs_putbuf(bp);
	return 0;
}

static void *
kmem_zalloc(size_t size, int flags)
{
	void	*ptr = malloc(size);
	memset(ptr, 0, size);
	return ptr;
}
static int
libxfs_initialize_perag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agcount,
	xfs_agnumber_t	*maxagi)
{
	xfs_agnumber_t	index, max_metadata;
	xfs_agnumber_t	first_initialised = 0;
	xfs_perag_t	*pag;
	xfs_agino_t	agino;
	xfs_ino_t	ino;
	xfs_sb_t	*sbp = &mp->m_sb;
	int		error = -ENOMEM;

	/*
	 * Walk the current per-ag tree so we don't try to initialise AGs
	 * that already exist (growfs case). Allocate and insert all the
	 * AGs we don't find ready for initialisation.
	 */
	for (index = 0; index < agcount; index++) {
		pag = xfs_perag_get(mp, index);
		if (pag) {
			xfs_perag_put(pag);
			continue;
		}
		if (!first_initialised)
			first_initialised = index;

		pag = kmem_zalloc(sizeof(*pag), KM_MAYFAIL);
		if (!pag)
			goto out_unwind;
		pag->pag_agno = index;
		pag->pag_mount = mp;

		if (radix_tree_insert(&mp->m_perag_tree, index, pag)) {
			error = -EEXIST;
			goto out_unwind;
		}
	}

	/*
	 * If we mount with the inode64 option, or no inode overflows
	 * the legacy 32-bit address space clear the inode32 option.
	 */
	agino = XFS_OFFBNO_TO_AGINO(mp, sbp->sb_agblocks - 1, 0);
	ino = XFS_AGINO_TO_INO(mp, agcount - 1, agino);

	if ((mp->m_flags & XFS_MOUNT_SMALL_INUMS) && ino > XFS_MAXINUMBER_32)
		mp->m_flags |= XFS_MOUNT_32BITINODES;
	else
		mp->m_flags &= ~XFS_MOUNT_32BITINODES;

	if (mp->m_flags & XFS_MOUNT_32BITINODES) {
		/*
		 * Calculate how much should be reserved for inodes to meet
		 * the max inode percentage.
		 */
		if (mp->m_maxicount) {
			__uint64_t	icount;

			icount = sbp->sb_dblocks * sbp->sb_imax_pct;
			do_div(icount, 100);
			icount += sbp->sb_agblocks - 1;
			do_div(icount, sbp->sb_agblocks);
			max_metadata = icount;
		} else {
			max_metadata = agcount;
		}

		for (index = 0; index < agcount; index++) {
			ino = XFS_AGINO_TO_INO(mp, index, agino);
			if (ino > XFS_MAXINUMBER_32) {
				index++;
				break;
			}

			pag = xfs_perag_get(mp, index);
			pag->pagi_inodeok = 1;
			if (index < max_metadata)
				pag->pagf_metadata = 1;
			xfs_perag_put(pag);
		}
	} 
    else {
		for (index = 0; index < agcount; index++)
        {
		//	pag = xfs_perag_get(mp, index);
	//		pag->pagi_inodeok = 1;
	//		xfs_perag_put(pag);
		}
	}

	if (maxagi)
		*maxagi = index;

	//mp->m_ag_prealloc_blocks = xfs_prealloc_blocks(mp);
	return 0;

out_unwind:
	free(pag);
	for (; index > first_initialised; index--) {
		pag = radix_tree_delete(&mp->m_perag_tree, index);
		free(pag);
	}
	return error;
}

static struct xfs_buftarg *
libxfs_buftarg_alloc(
	struct xfs_mount	*mp,
	dev_t			dev)
{
	struct xfs_buftarg	*btp;

	btp = malloc(sizeof(*btp));
	if (!btp) {
		exit(1);
	}
	btp->bt_mount = mp;
	btp->dev = dev;
	return btp;
}

void
libxfs_buftarg_init(
	struct xfs_mount	*mp,
	dev_t			dev,
	dev_t			logdev,
	dev_t			rtdev)
{
	if (mp->m_ddev_targp) {
		/* should already have all buftargs initialised */
		if (mp->m_ddev_targp->dev != dev ||
		    mp->m_ddev_targp->bt_mount != mp) {
			exit(1);
		}
		if (!logdev || logdev == dev) {
			if (mp->m_logdev_targp != mp->m_ddev_targp) {
				exit(1);
			}
		} else if (mp->m_logdev_targp->dev != logdev ||
			   mp->m_logdev_targp->bt_mount != mp) {
			exit(1);
		}
		if (rtdev && (mp->m_rtdev_targp->dev != rtdev ||
			      mp->m_rtdev_targp->bt_mount != mp)) {
			exit(1);
		}
		return;
	}

	mp->m_ddev_targp = libxfs_buftarg_alloc(mp, dev);
	if (!logdev || logdev == dev)
		mp->m_logdev_targp = mp->m_ddev_targp;
	else
		mp->m_logdev_targp = libxfs_buftarg_alloc(mp, logdev);
	mp->m_rtdev_targp = libxfs_buftarg_alloc(mp, rtdev);
}
static uint
xfs_btree_compute_maxlevels(
    struct xfs_mount    *mp,
    uint            *limits,
    unsigned long       len)
{
    uint            level;
    unsigned long       maxblocks;

    maxblocks = (len + limits[0] - 1) / limits[0];
    for (level = 1; maxblocks > 1; level++)
    {     
        maxblocks = (maxblocks + limits[1] - 1) / limits[1];
    }
    return level;
}

static void
xfs_refcountbt_compute_maxlevels(
    struct xfs_mount        *mp)
{    
    mp->m_refc_maxlevels = xfs_btree_compute_maxlevels(mp,
            mp->m_refc_mnr, mp->m_sb.sb_agblocks);
}
static void
xfs_ialloc_compute_maxlevels(
    xfs_mount_t *mp)        /* file system mount structure */
{
    uint        inodes;

    inodes = (1LL << XFS_INO_AGINO_BITS(mp)) >> XFS_INODES_PER_CHUNK_LOG;
    g_print ("xfs_ialloc_compute_maxlevels \r\n");
    mp->m_in_maxlevels = xfs_btree_compute_maxlevels(mp, mp->m_inobt_mnr,
                             inodes);
}
static void
xfs_alloc_compute_maxlevels(
    xfs_mount_t *mp)    /* file system mount structure */
{
    mp->m_ag_maxlevels = xfs_btree_compute_maxlevels(mp, mp->m_alloc_mnr,
            (mp->m_sb.sb_agblocks + 1) / 2);
}

static void
xfs_rmapbt_compute_maxlevels(
	struct xfs_mount		*mp)
{
	if (xfs_sb_version_hasreflink(&mp->m_sb))
		mp->m_rmap_maxlevels = XFS_BTREE_MAXLEVELS;
	else
		mp->m_rmap_maxlevels = xfs_btree_compute_maxlevels(mp,
				mp->m_rmap_mnr, mp->m_sb.sb_agblocks);
}
static int
xfs_bmdr_maxrecs(
	int			blocklen,
	int			leaf)
{
	blocklen -= sizeof(xfs_bmdr_block_t);

	if (leaf)
		return blocklen / sizeof(xfs_bmdr_rec_t);
	return blocklen / (sizeof(xfs_bmdr_key_t) + sizeof(xfs_bmdr_ptr_t));
}
static void
xfs_bmap_compute_maxlevels(
	xfs_mount_t	*mp,		/* file system mount structure */
	int		whichfork)	/* data or attr fork */
{
	int		level;		/* btree level */
	uint		maxblocks;	/* max blocks at this level */
	uint		maxleafents;	/* max leaf entries possible */
	int		maxrootrecs;	/* max records in root block */
	int		minleafrecs;	/* min records in leaf block */
	int		minnoderecs;	/* min records in node block */
	int		sz;		/* root block size */
	if (whichfork == XFS_DATA_FORK) {
		maxleafents = MAXEXTNUM;
		sz = XFS_BMDR_SPACE_CALC(MINDBTPTRS);
	} else {
		maxleafents = MAXAEXTNUM;
		sz = XFS_BMDR_SPACE_CALC(MINABTPTRS);
	}
	maxrootrecs = xfs_bmdr_maxrecs(sz, 0);
	minleafrecs = mp->m_bmap_dmnr[0];
	minnoderecs = mp->m_bmap_dmnr[1];
	maxblocks = (maxleafents + minleafrecs - 1) / minleafrecs;
	for (level = 1; maxblocks > 1; level++) {
		if (maxblocks <= maxrootrecs)
			maxblocks = 1;
		else
			maxblocks = (maxblocks + minnoderecs - 1) / minnoderecs;
	}
	mp->m_bm_maxlevels[whichfork] = level;
}
static void
xfs_trans_init(
	struct xfs_mount	*mp)
{
	xfs_trans_resv_calc(mp, &mp->m_resv);
}
static int
xfs_da_mount(
	struct xfs_mount	*mp)
{
	struct xfs_da_geometry	*dageo;
	int			nodehdr_size;

	mp->m_dir_inode_ops = xfs_dir_get_ops(mp, NULL);
	mp->m_nondir_inode_ops = xfs_nondir_get_ops(mp, NULL);

	nodehdr_size = mp->m_dir_inode_ops->node_hdr_size;
	mp->m_dir_geo = kmem_zalloc(sizeof(struct xfs_da_geometry),
				    KM_SLEEP | KM_MAYFAIL);
	mp->m_attr_geo = kmem_zalloc(sizeof(struct xfs_da_geometry),
				     KM_SLEEP | KM_MAYFAIL);
	if (!mp->m_dir_geo || !mp->m_attr_geo) {
		free(mp->m_dir_geo);
		free(mp->m_attr_geo);
		return -ENOMEM;
	}
	/* set up directory geometry */
	dageo = mp->m_dir_geo;
	dageo->blklog = mp->m_sb.sb_blocklog + mp->m_sb.sb_dirblklog;
	dageo->fsblog = mp->m_sb.sb_blocklog;
	dageo->blksize = 1 << dageo->blklog;
	dageo->fsbcount = 1 << mp->m_sb.sb_dirblklog;

	dageo->datablk = xfs_dir2_byte_to_da(dageo, XFS_DIR2_DATA_OFFSET);
	dageo->leafblk = xfs_dir2_byte_to_da(dageo, XFS_DIR2_LEAF_OFFSET);
	dageo->freeblk = xfs_dir2_byte_to_da(dageo, XFS_DIR2_FREE_OFFSET);
	dageo->node_ents = (dageo->blksize - nodehdr_size) /
				(uint)sizeof(xfs_da_node_entry_t);
	dageo->magicpct = (dageo->blksize * 37) / 100;

	dageo = mp->m_attr_geo;
	dageo->blklog = mp->m_sb.sb_blocklog;
	dageo->fsblog = mp->m_sb.sb_blocklog;
	dageo->blksize = 1 << dageo->blklog;
	dageo->fsbcount = 1;
	dageo->node_ents = (dageo->blksize - nodehdr_size) /
				(uint)sizeof(xfs_da_node_entry_t);
	dageo->magicpct = (dageo->blksize * 37) / 100;
	return 0;
}
xfs_mount_t *
libxfs_mount(
	xfs_mount_t	*mp,
	xfs_sb_t	*sb,
	dev_t		dev,
	dev_t		logdev,
	dev_t		rtdev,
	int		flags)
{
	xfs_daddr_t	d;
	xfs_buf_t	*bp;
	xfs_sb_t	*sbp;
	int		error;

	libxfs_buftarg_init(mp, dev, logdev, rtdev);

	mp->m_flags = (LIBXFS_MOUNT_32BITINODES|LIBXFS_MOUNT_32BITINOOPT);
	mp->m_sb = *sb;
	INIT_RADIX_TREE(&mp->m_perag_tree, 0);
	sbp = &(mp->m_sb);

	xfs_sb_mount_common(mp, sb);
	xfs_alloc_compute_maxlevels(mp);
	xfs_bmap_compute_maxlevels(mp, XFS_DATA_FORK);
	xfs_bmap_compute_maxlevels(mp, XFS_ATTR_FORK);
	xfs_ialloc_compute_maxlevels(mp);
	xfs_rmapbt_compute_maxlevels(mp);
	xfs_refcountbt_compute_maxlevels(mp);

	if (sbp->sb_imax_pct) {
		/* Make sure the maximum inode count is a multiple of the
		 * units we allocate inodes in.
		 */
		mp->m_maxicount = (sbp->sb_dblocks * sbp->sb_imax_pct) / 100;
		mp->m_maxicount = ((mp->m_maxicount / mp->m_ialloc_blks) *
				  mp->m_ialloc_blks)  << sbp->sb_inopblog;
	} else
		mp->m_maxicount = 0;

	mp->m_inode_cluster_size = XFS_INODE_BIG_CLUSTER_SIZE;

	/*
	 * Set whether we're using stripe alignment.
	 */
	if (xfs_sb_version_hasdalign(&mp->m_sb)) {
		mp->m_dalign = sbp->sb_unit;
		mp->m_swidth = sbp->sb_width;
	}

	/*
	 * Set whether we're using inode alignment.
	 */
	if (xfs_sb_version_hasalign(&mp->m_sb) &&
	    mp->m_sb.sb_inoalignmt >=
	    XFS_B_TO_FSBT(mp, mp->m_inode_cluster_size))
		mp->m_inoalign_mask = mp->m_sb.sb_inoalignmt - 1;
	else
		mp->m_inoalign_mask = 0;
	/*
	 * If we are using stripe alignment, check whether
	 * the stripe unit is a multiple of the inode alignment
	 */
	if (mp->m_dalign && mp->m_inoalign_mask &&
					!(mp->m_dalign & mp->m_inoalign_mask))
		mp->m_sinoalign = mp->m_dalign;
	else
		mp->m_sinoalign = 0;

	/*
	 * Check that the data (and log if separate) are an ok size.
	 */
	d = (xfs_daddr_t) XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks);
	if (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_dblocks) {
		if (!(flags & LIBXFS_MOUNT_DEBUGGER))
			return NULL;
	}

	/*
	 * We automatically convert v1 inodes to v2 inodes now, so if
	 * the NLINK bit is not set we can't operate on the filesystem.
	 */
	if (!(sbp->sb_versionnum & XFS_SB_VERSION_NLINKBIT)) {

		exit(1);
	}

	/* Check for supported directory formats */
	if (!(sbp->sb_versionnum & XFS_SB_VERSION_DIRV2BIT)) {

		exit(1);
	}

	/* check for unsupported other features */
	if (!xfs_sb_good_version(sbp)) {
		exit(1);
	}

	xfs_da_mount(mp);

	if (xfs_sb_version_hasattr2(&mp->m_sb))
		mp->m_flags |= LIBXFS_MOUNT_ATTR2;

	/* Initialize the precomputed transaction reservations values */
	xfs_trans_init(mp);

	if (dev == 0)	/* maxtrres, we have no device so leave now */
		return mp;

	bp = libxfs_readbuf(mp->m_dev,
			d - XFS_FSS_TO_BB(mp, 1), XFS_FSS_TO_BB(mp, 1),
			!(flags & LIBXFS_MOUNT_DEBUGGER), NULL);
	if (!bp) {
		if (!(flags & LIBXFS_MOUNT_DEBUGGER))
			return NULL;
	} else
		libxfs_putbuf(bp);

	if (mp->m_logdev_targp->dev &&
	    mp->m_logdev_targp->dev != mp->m_ddev_targp->dev) {
		d = (xfs_daddr_t) XFS_FSB_TO_BB(mp, mp->m_sb.sb_logblocks);
		if ( (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_logblocks) ||
		     (!(bp = libxfs_readbuf(mp->m_logdev_targp,
					d - XFS_FSB_TO_BB(mp, 1),
					XFS_FSB_TO_BB(mp, 1),
					!(flags & LIBXFS_MOUNT_DEBUGGER), NULL))) ) {
			if (!(flags & LIBXFS_MOUNT_DEBUGGER))
				return NULL;
		}
		if (bp)
			libxfs_putbuf(bp);
	}

	/* Initialize realtime fields in the mount structure */
	if (rtmount_init(mp, flags)) {
			return NULL;
	}

	error = libxfs_initialize_perag(mp, sbp->sb_agcount, &mp->m_maxagi);
	if (error) {
		exit(1);
	}
	return mp;
}

void
libxfs_destroy(void)
{
	manage_zones(1);
	cache_destroy(libxfs_bcache);
}

int
libxfs_device_alignment(void)
{
	return platform_align_blockdev();
}
