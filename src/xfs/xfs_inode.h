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

#ifndef __XFS_INODE_H__
#define __XFS_INODE_H__

/* These match kernel side includes */
#include "xfs_inode_buf.h"

#define	XFS_INLINE_EXTS		2
#define	XFS_INLINE_DATA		32
struct xfs_trans;
struct xfs_mount;
struct xfs_inode_log_item;
struct xfs_dir_ops;

/*
 * Inode interface. This fakes up a "VFS inode" to make the xfs_inode appear
 * similar to the kernel which now is used tohold certain parts of the on-disk
 * metadata.
 */
struct inode {
	mode_t		i_mode;
	uint32_t	i_nlink;
	uint32_t	i_generation;
	uint64_t	i_version;
	struct timespec	i_atime;
	struct timespec	i_mtime;
	struct timespec	i_ctime;
};

typedef struct xfs_ext_irec {
	xfs_bmbt_rec_host_t *er_extbuf;	/* block of extent records */
	xfs_extnum_t	er_extoff;	/* extent offset in file */
	xfs_extnum_t	er_extcount;	/* number of extents in page/block */
} xfs_ext_irec_t;
typedef struct xfs_ifork {
	int			if_bytes;	/* bytes in if_u1 */
	int			if_real_bytes;	/* bytes allocated in if_u1 */
	struct xfs_btree_block	*if_broot;	/* file's incore btree root */
	short			if_broot_bytes;	/* bytes allocated for root */
	unsigned char		if_flags;	/* per-fork flags */
	union {
		xfs_bmbt_rec_host_t *if_extents;/* linear map file exts */
		xfs_ext_irec_t	*if_ext_irec;	/* irec map file exts */
		char		*if_data;	/* inline file data */
	} if_u1;
	union {
		xfs_bmbt_rec_host_t if_inline_ext[XFS_INLINE_EXTS];
						/* very small file extents */
		char		if_inline_data[XFS_INLINE_DATA];
						/* very small file data */
		xfs_dev_t	if_rdev;	/* dev number if special */
		uuid_t		if_uuid;	/* mount point value */
	} if_u2;
} xfs_ifork_t;
typedef struct xfs_inode {
	struct cache_node	i_node;
	struct xfs_mount	*i_mount;	/* fs mount struct ptr */
	xfs_ino_t		i_ino;		/* inode number (agno/agino) */
	struct xfs_imap		i_imap;		/* location for xfs_imap() */
	struct xfs_buftarg	i_dev;		/* dev for this inode */
	struct xfs_ifork	*i_afp;		/* attribute fork pointer */
	struct xfs_ifork	*i_cowfp;	/* copy on write extents */
	struct xfs_ifork	i_df;		/* data fork */
	struct xfs_trans	*i_transp;	/* ptr to owning transaction */
	struct xfs_inode_log_item *i_itemp;	/* logging information */
	unsigned int		i_delayed_blks;	/* count of delay alloc blks */
	struct xfs_icdinode	i_d;		/* most of ondisk inode */

	xfs_extnum_t		i_cnextents;	/* # of extents in cow fork */
	unsigned int		i_cformat;	/* format of cow fork */

	xfs_fsize_t		i_size;		/* in-memory size */
	const struct xfs_dir_ops *d_ops;	/* directory ops vector */
	struct inode		i_vnode;
} xfs_inode_t;

static inline struct inode *VFS_I(struct xfs_inode *ip)
{
	return &ip->i_vnode;
}

/*
 * wrappers around the mode checks to simplify code
 */
static inline bool XFS_ISREG(struct xfs_inode *ip)
{
	return S_ISREG(VFS_I(ip)->i_mode);
}

static inline bool XFS_ISDIR(struct xfs_inode *ip)
{
	return S_ISDIR(VFS_I(ip)->i_mode);
}

/*
 * For regular files we only update the on-disk filesize when actually
 * writing data back to disk.  Until then only the copy in the VFS inode
 * is uptodate.
 */
static inline xfs_fsize_t XFS_ISIZE(struct xfs_inode *ip)
{
	if (XFS_ISREG(ip))
		return ip->i_size;
	return ip->i_d.di_size;
}
#define XFS_IS_REALTIME_INODE(ip) ((ip)->i_d.di_flags & XFS_DIFLAG_REALTIME)

/* inode link counts */
static inline void set_nlink(struct inode *inode, uint32_t nlink)
{
	inode->i_nlink = nlink;
}
static inline void inc_nlink(struct inode *inode)
{
	inode->i_nlink++;
}

/*
 * Project quota id helpers (previously projid was 16bit only and using two
 * 16bit values to hold new 32bit projid was chosen to retain compatibility with
 * "old" filesystems).
 *
 * Copied here from xfs_inode.h because it has to be defined after the struct
 * xfs_inode...
 */
static inline prid_t
xfs_get_projid(struct xfs_icdinode *id)
{
	return (prid_t)id->di_projid_hi << 16 | id->di_projid_lo;
}

static inline void
xfs_set_projid(struct xfs_icdinode *id, prid_t projid)
{
	id->di_projid_hi = (__uint16_t) (projid >> 16);
	id->di_projid_lo = (__uint16_t) (projid & 0xffff);
}

static inline bool xfs_is_reflink_inode(struct xfs_inode *ip)
{
	return ip->i_d.di_flags2 & XFS_DIFLAG2_REFLINK;
}

typedef struct cred {
	uid_t	cr_uid;
	gid_t	cr_gid;
} cred_t;

extern void	libxfs_trans_ichgtime(struct xfs_trans *,
				struct xfs_inode *, int);


#endif /* __XFS_INODE_H__ */
