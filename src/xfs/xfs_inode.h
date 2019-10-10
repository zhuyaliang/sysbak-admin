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


#define	XFS_INLINE_EXTS		2
#define	XFS_INLINE_DATA		32
struct xfs_trans;
struct xfs_mount;
struct xfs_inode_log_item;
struct xfs_dir_ops;

struct inode {
	mode_t		i_mode;
	uint32_t	i_nlink;
	uint32_t	i_generation;
	uint64_t	i_version;
	struct timespec	i_atime;
	struct timespec	i_mtime;
	struct timespec	i_ctime;
};

typedef struct xfs_bmbt_rec_host1 {
    __uint64_t      l0, l1;
} xfs_bmbt_rec_host_t1;

typedef struct xfs_ext_irec {
	xfs_bmbt_rec_host_t1 *er_extbuf;	/* block of extent records */
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
		xfs_bmbt_rec_host_t1 *if_extents;/* linear map file exts */
		xfs_ext_irec_t	*if_ext_irec;	/* irec map file exts */
		char		*if_data;	/* inline file data */
	} if_u1;
	union {
		xfs_bmbt_rec_host_t1 if_inline_ext[XFS_INLINE_EXTS];
						/* very small file extents */
		char		if_inline_data[XFS_INLINE_DATA];
						/* very small file data */
		xfs_dev_t	if_rdev;	/* dev number if special */
		uuid_t		if_uuid;	/* mount point value */
	} if_u2;
} xfs_ifork_t;

struct xfs_imap {
	xfs_daddr_t	im_blkno;	/* starting BB of inode chunk */
	unsigned short	im_len;		/* length in BBs of inode chunk */
	unsigned short	im_boffset;	/* inode offset in block in bytes */
};

struct xfs_icdinode {
	__int8_t	di_version;	/* inode version */
	__int8_t	di_format;	/* format of di_c data */
	__uint16_t	di_flushiter;	/* incremented on flush */
	__uint32_t	di_uid;		/* owner's user id */
	__uint32_t	di_gid;		/* owner's group id */
	__uint16_t	di_projid_lo;	/* lower part of owner's project id */
	__uint16_t	di_projid_hi;	/* higher part of owner's project id */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	xfs_rfsblock_t	di_nblocks;	/* # of direct & btree blocks used */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	xfs_extnum_t	di_nextents;	/* number of extents in data fork */
	xfs_aextnum_t	di_anextents;	/* number of extents in attribute fork*/
	__uint8_t	di_forkoff;	/* attr fork offs, <<3 for 64b align */
	__int8_t	di_aformat;	/* format of attr fork's data */
	__uint32_t	di_dmevmask;	/* DMIG event mask */
	__uint16_t	di_dmstate;	/* DMIG state info */
	__uint16_t	di_flags;	/* random flags, XFS_DIFLAG_... */
	__uint64_t	di_flags2;	/* more random flags */
	__uint32_t	di_cowextsize;	/* basic cow extent size for file */
	xfs_ictimestamp_t di_crtime;	/* time created */
};
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
#endif /* __XFS_INODE_H__ */
