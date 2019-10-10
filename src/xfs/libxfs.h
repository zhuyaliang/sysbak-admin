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

#ifndef __LIBXFS_H__
#define __LIBXFS_H__

//#include "libxfs_api_defs.h"
//#include "platform_defs.h"
#include <xfs/xfs.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/types.h>
#include <limits.h>
#include <stdbool.h>
#include <libgen.h>
#include "list.h"
#include "radix-tree.h"
#include "cache.h"
//#include "bitops.h"
//#include "kmem.h"
#include <xfs/xfs_types.h>
#include <xfs/xfs_fs.h>
#include <xfs/xfs_arch.h>

//#include "xfs_shared.h"
#include <xfs/xfs_format.h>
#include <xfs/xfs_log_format.h>
#include "xfs_quota_defs.h"
#include "xfs_trans_resv.h"

/*
 * This mirrors the kernel include for xfs_buf.h - it's implicitly included in
 * every files via a similar include in the kernel xfs_linux.h.
 */
#include "libxfs_io.h"

#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include <xfs/xfs_da_format.h>
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
//#include "xfs_alloc_btree.h"
#include "xfs_inode_fork.h"
#include "xfs_inode_buf.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
//#include "xfs_trace.h"
//#include "xfs_trans.h"
#include "xfs_rmap_btree.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC 0x58465342
#endif

#define xfs_isset(a,i)	((a)[(i)/(sizeof(*(a))*NBBY)] & (1ULL<<((i)%(sizeof(*(a))*NBBY))))
#define XFS_ALLOC_BLOCK_LEN(mp) \
	(xfs_sb_version_hascrc(&((mp)->m_sb)) ? \
		XFS_BTREE_SBLOCK_CRC_LEN : XFS_BTREE_SBLOCK_LEN)
#define XFS_ALLOC_PTR_ADDR(mp, block, index, maxrecs) \
	((xfs_alloc_ptr_t *) \
		((char *)(block) + \
		 XFS_ALLOC_BLOCK_LEN(mp) + \
		 (maxrecs) * sizeof(xfs_alloc_key_t) + \
		 ((index) - 1) * sizeof(xfs_alloc_ptr_t)))

#define XFS_ALLOC_REC_ADDR(mp, block, index) \
	((xfs_alloc_rec_t *) \
		((char *)(block) + \
		 XFS_ALLOC_BLOCK_LEN(mp) + \
		 (((index) - 1) * sizeof(xfs_alloc_rec_t))))

typedef struct kmem_zone {
	int	zone_unitsize;	/* Size in bytes of zone unit           */
	char	*zone_name;	/* tag name                             */
	int	allocated;	/* debug: How many currently allocated  */
} kmem_zone_t;
/*
 * Argument structure for libxfs_init().
 */
typedef struct {
				/* input parameters */
	char            *volname;       /* pathname of volume */
	char            *dname;         /* pathname of data "subvolume" */
	char            *logname;       /* pathname of log "subvolume" */
	char            *rtname;        /* pathname of realtime "subvolume" */
	int             isreadonly;     /* filesystem is only read in applic */
	int             isdirect;       /* we can attempt to use direct I/O */
	int             disfile;        /* data "subvolume" is a regular file */
	int             dcreat;         /* try to create data subvolume */
	int             lisfile;        /* log "subvolume" is a regular file */
	int             lcreat;         /* try to create log subvolume */
	int             risfile;        /* realtime "subvolume" is a reg file */
	int             rcreat;         /* try to create realtime subvolume */
	int		setblksize;	/* attempt to set device blksize */
	int		usebuflock;	/* lock xfs_buf_t's - for MT usage */
				/* output results */
	dev_t           ddev;           /* device for data subvolume */
	dev_t           logdev;         /* device for log subvolume */
	dev_t           rtdev;          /* device for realtime subvolume */
	long long       dsize;          /* size of data subvolume (BBs) */
	long long       logBBsize;      /* size of log subvolume (BBs) */
					/* (blocks allocated for use as
					 * log is stored in mount structure) */
	long long       logBBstart;     /* start block of log subvolume (BBs) */
	long long       rtsize;         /* size of realtime subvolume (BBs) */
	int		dbsize;		/* data subvolume device blksize */
	int		lbsize;		/* log subvolume device blksize */
	int		rtbsize;	/* realtime subvolume device blksize */
	int             dfd;            /* data subvolume file descriptor */
	int             logfd;          /* log subvolume file descriptor */
	int             rtfd;           /* realtime subvolume file descriptor */
	int		icache_flags;	/* cache init flags */
	int		bcache_flags;	/* cache init flags */
} libxfs_init_t;

#define LIBXFS_EXIT_ON_FAILURE	0x0001	/* exit the program if a call fails */
#define LIBXFS_ISREADONLY	0x0002	/* disallow all mounted filesystems */
#define LIBXFS_ISINACTIVE	0x0004	/* allow mounted only if mounted ro */
#define LIBXFS_DANGEROUSLY	0x0008	/* repairing a device mounted ro    */
#define LIBXFS_EXCLUSIVELY	0x0010	/* disallow other accesses (O_EXCL) */
#define LIBXFS_DIRECT		0x0020	/* can use direct I/O, not buffered */

extern xfs_lsn_t libxfs_max_lsn;
extern int	libxfs_init (libxfs_init_t *);
void radix_tree_init(void);
extern void	libxfs_destroy (void);
extern int	libxfs_device_to_fd (dev_t);
extern dev_t	libxfs_device_open (char *, int, int, int);
extern void	libxfs_device_close (dev_t);
extern int	libxfs_device_alignment (void);
extern void	libxfs_report(FILE *);
extern void	platform_findsizes(char *path, int fd, long long *sz, int *bsz);
extern int	platform_nproc(void);

/* check or write log footer: specify device, log size in blocks & uuid */
typedef char	*(libxfs_get_block_t)(char *, int, void *);

/*
 * Helpers to clear the log to a particular log cycle.
 */
#define XLOG_INIT_CYCLE	1

/* Shared utility routines */
extern unsigned int	libxfs_log2_roundup(unsigned int i);

extern void	libxfs_fs_cmn_err(int, struct xfs_mount *, char *, ...);

/* XXX: this is messy and needs fixing */
#ifndef __LIBXFS_INTERNAL_XFS_H__
extern void cmn_err(int, char *, ...);
enum ce { CE_DEBUG, CE_CONT, CE_NOTE, CE_WARN, CE_ALERT, CE_PANIC };
#endif


extern int		libxfs_nproc(void);
extern unsigned long	libxfs_physmem(void);	/* in kilobytes */

//#include "xfs_ialloc.h"
//#include "xfs_trans_space.h"

#define XFS_INOBT_IS_FREE_DISK(rp,i)		\
			((be64_to_cpu((rp)->ir_free) & XFS_INOBT_MASK(i)) != 0)

static inline bool
xfs_inobt_is_sparse_disk(
	struct xfs_inobt_rec	*rp,
	int			offset)
{
	int			spshift;
	uint16_t		holemask;

	holemask = be16_to_cpu(rp->ir_u.sp.ir_holemask);
	spshift = offset / XFS_INODES_PER_HOLEMASK_BIT;
	if ((1 << spshift) & holemask)
		return true;

	return false;
}

void
xfs_sb_mount_common(
	struct xfs_mount *mp,
	struct xfs_sb	*sbp);
void
xfs_sb_from_disk(
	struct xfs_sb	*to,
	xfs_dsb_t	*from);
/* XXX: need parts of xfs_attr.h in userspace */
#define LIBXFS_ATTR_ROOT	0x0002	/* use attrs in root namespace */
#define LIBXFS_ATTR_SECURE	0x0008	/* use attrs in security namespace */
#define LIBXFS_ATTR_CREATE	0x0010	/* create, but fail if attr exists */
#define LIBXFS_ATTR_REPLACE	0x0020	/* set, but fail if attr not exists */

#endif	/* __LIBXFS_H__ */
