/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
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
#ifndef __XFS_DA_BTREE_H__
#define	__XFS_DA_BTREE_H__

struct xfs_defer_ops;
struct xfs_inode;
struct xfs_trans;
struct zone;
struct xfs_dir_ops;

/*
 * Directory/attribute geometry information. There will be one of these for each
 * data fork type, and it will be passed around via the xfs_da_args. Global
 * structures will be attached to the xfs_mount.
 */
struct xfs_da_geometry {
	int		blksize;	/* da block size in bytes */
	int		fsbcount;	/* da block size in filesystem blocks */
	uint8_t		fsblog;		/* log2 of _filesystem_ block size */
	uint8_t		blklog;		/* log2 of da block size */
	uint		node_ents;	/* # of entries in a danode */
	int		magicpct;	/* 37% of block size in bytes */
	xfs_dablk_t	datablk;	/* blockno of dir data v2 */
	xfs_dablk_t	leafblk;	/* blockno of leaf data v2 */
	xfs_dablk_t	freeblk;	/* blockno of free data v2 */
};

/*========================================================================
 * Btree searching and modification structure definitions.
 *========================================================================*/

/*
 * Search comparison results
 */
enum xfs_dacmp {
	XFS_CMP_DIFFERENT,	/* names are completely different */
	XFS_CMP_EXACT,		/* names are exactly the same */
	XFS_CMP_CASE		/* names are same but differ in case */
};

/*
 * Structure to ease passing around component names.
 */
typedef struct xfs_da_args {
	struct xfs_da_geometry *geo;	/* da block geometry */
	const __uint8_t	*name;		/* string (maybe not NULL terminated) */
	int		namelen;	/* length of string (maybe no NULL) */
	__uint8_t	filetype;	/* filetype of inode for directories */
	__uint8_t	*value;		/* set of bytes (maybe contain NULLs) */
	int		valuelen;	/* length of value */
	int		flags;		/* argument flags (eg: ATTR_NOCREATE) */
	xfs_dahash_t	hashval;	/* hash value of name */
	xfs_ino_t	inumber;	/* input/output inode number */
	struct xfs_inode *dp;		/* directory inode to manipulate */
	xfs_fsblock_t	*firstblock;	/* ptr to firstblock for bmap calls */
	struct xfs_defer_ops *dfops;	/* ptr to freelist for bmap_finish */
	struct xfs_trans *trans;	/* current trans (changes over time) */
	xfs_extlen_t	total;		/* total blocks needed, for 1st bmap */
	int		whichfork;	/* data or attribute fork */
	xfs_dablk_t	blkno;		/* blkno of attr leaf of interest */
	int		index;		/* index of attr of interest in blk */
	xfs_dablk_t	rmtblkno;	/* remote attr value starting blkno */
	int		rmtblkcnt;	/* remote attr value block count */
	int		rmtvaluelen;	/* remote attr value length in bytes */
	xfs_dablk_t	blkno2;		/* blkno of 2nd attr leaf of interest */
	int		index2;		/* index of 2nd attr in blk */
	xfs_dablk_t	rmtblkno2;	/* remote attr value starting blkno */
	int		rmtblkcnt2;	/* remote attr value block count */
	int		rmtvaluelen2;	/* remote attr value length in bytes */
	int		op_flags;	/* operation flags */
	enum xfs_dacmp	cmpresult;	/* name compare result for lookups */
} xfs_da_args_t;

typedef struct xfs_da_state_blk {
	struct xfs_buf	*bp;		/* buffer containing block */
	xfs_dablk_t	blkno;		/* filesystem blkno of buffer */
	xfs_daddr_t	disk_blkno;	/* on-disk blkno (in BBs) of buffer */
	int		index;		/* relevant index into block */
	xfs_dahash_t	hashval;	/* last hash value in block */
	int		magic;		/* blk's magic number, ie: blk type */
} xfs_da_state_blk_t;

typedef struct xfs_da_state_path {
	int			active;		/* number of active levels */
	xfs_da_state_blk_t	blk[XFS_DA_NODE_MAXDEPTH];
} xfs_da_state_path_t;

typedef struct xfs_da_state {
	xfs_da_args_t		*args;		/* filename arguments */
	struct xfs_mount	*mp;		/* filesystem mount point */
	xfs_da_state_path_t	path;		/* search/split paths */
	xfs_da_state_path_t	altpath;	/* alternate path for join */
	unsigned char		inleaf;		/* insert into 1->lf, 0->splf */
	unsigned char		extravalid;	/* T/F: extrablk is in use */
	unsigned char		extraafter;	/* T/F: extrablk is after new */
	xfs_da_state_blk_t	extrablk;	/* for double-splits on leaves */
						/* for dirv2 extrablk is data */
} xfs_da_state_t;
#endif	/* __XFS_DA_BTREE_H__ */
