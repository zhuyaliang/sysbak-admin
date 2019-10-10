/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_BTREE_H__
#define	__XFS_BTREE_H__

#include "xfs_bmap.h"
struct xfs_buf;
struct xfs_defer_ops;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

union xfs_btree_ptr {
	__be32			s;	/* short form ptr */
	__be64			l;	/* long form ptr */
};

union xfs_btree_key {
	struct xfs_bmbt_key		bmbt;
	xfs_bmdr_key_t			bmbr;	/* bmbt root block */
	xfs_alloc_key_t			alloc;
	struct xfs_inobt_key		inobt;
	struct xfs_rmap_key		rmap;
	struct xfs_rmap_key		__rmap_bigkey[2];
	struct xfs_refcount_key		refc;
};

union xfs_btree_rec {
	struct xfs_bmbt_rec		bmbt;
	xfs_bmdr_rec_t			bmbr;	/* bmbt root block */
	struct xfs_alloc_rec		alloc;
	struct xfs_inobt_rec		inobt;
	struct xfs_rmap_rec		rmap;
	struct xfs_refcount_rec		refc;
};
#define	XFS_BTNUM_BNO	((xfs_btnum_t)XFS_BTNUM_BNOi)
#define	XFS_BTREE_MAXLEVELS	9	/* max of all btrees */

struct xfs_btree_ops {
	size_t	key_len;
	size_t	rec_len;

	struct xfs_btree_cur *(*dup_cursor)(struct xfs_btree_cur *);
	void	(*update_cursor)(struct xfs_btree_cur *src,
				 struct xfs_btree_cur *dst);

	void	(*set_root)(struct xfs_btree_cur *cur,
			    union xfs_btree_ptr *nptr, int level_change);

	int	(*alloc_block)(struct xfs_btree_cur *cur,
			       union xfs_btree_ptr *start_bno,
			       union xfs_btree_ptr *new_bno,
			       int *stat);
	int	(*free_block)(struct xfs_btree_cur *cur, struct xfs_buf *bp);

	void	(*update_lastrec)(struct xfs_btree_cur *cur,
				  struct xfs_btree_block *block,
				  union xfs_btree_rec *rec,
				  int ptr, int reason);

	int	(*get_minrecs)(struct xfs_btree_cur *cur, int level);
	int	(*get_maxrecs)(struct xfs_btree_cur *cur, int level);
	int	(*get_dmaxrecs)(struct xfs_btree_cur *cur, int level);
	void	(*init_key_from_rec)(union xfs_btree_key *key,
				     union xfs_btree_rec *rec);
	void	(*init_rec_from_cur)(struct xfs_btree_cur *cur,
				     union xfs_btree_rec *rec);
	void	(*init_ptr_from_cur)(struct xfs_btree_cur *cur,
				     union xfs_btree_ptr *ptr);
	void	(*init_high_key_from_rec)(union xfs_btree_key *key,
					  union xfs_btree_rec *rec);

	__int64_t (*key_diff)(struct xfs_btree_cur *cur,
			      union xfs_btree_key *key);

	__int64_t (*diff_two_keys)(struct xfs_btree_cur *cur,
				   union xfs_btree_key *key1,
				   union xfs_btree_key *key2);

	const struct xfs_buf_ops	*buf_ops;

#if defined(DEBUG) || defined(XFS_WARN)
	int	(*keys_inorder)(struct xfs_btree_cur *cur,
				union xfs_btree_key *k1,
				union xfs_btree_key *k2);

	int	(*recs_inorder)(struct xfs_btree_cur *cur,
				union xfs_btree_rec *r1,
				union xfs_btree_rec *r2);
#endif
};

/*
 * Reasons for the update_lastrec method to be called.
 */
#define LASTREC_UPDATE	0
#define LASTREC_INSREC	1
#define LASTREC_DELREC	2


union xfs_btree_irec {
	struct xfs_alloc_rec_incore	a;
	struct xfs_bmbt_irec1		b;
	struct xfs_inobt_rec_incore	i;
	struct xfs_rmap_irec		r;
	struct xfs_refcount_irec	rc;
};

/* Per-AG btree private information. */
union xfs_btree_cur_private {
	struct {
		unsigned long	nr_ops;		/* # record updates */
		int		shape_changes;	/* # of extent splits */
	} refc;
};

/*
 * Btree cursor structure.
 * This collects all information needed by the btree code in one place.
 */
typedef struct xfs_btree_cur
{
	struct xfs_trans	*bc_tp;	/* transaction we're in, if any */
	struct xfs_mount	*bc_mp;	/* file system mount struct */
	const struct xfs_btree_ops *bc_ops;
	uint			bc_flags; /* btree features - below */
	union xfs_btree_irec	bc_rec;	/* current insert/search record value */
	struct xfs_buf	*bc_bufs[XFS_BTREE_MAXLEVELS];	/* buf ptr per level */
	int		bc_ptrs[XFS_BTREE_MAXLEVELS];	/* key/record # */
	__uint8_t	bc_ra[XFS_BTREE_MAXLEVELS];	/* readahead bits */
#define	XFS_BTCUR_LEFTRA	1	/* left sibling has been read-ahead */
#define	XFS_BTCUR_RIGHTRA	2	/* right sibling has been read-ahead */
	__uint8_t	bc_nlevels;	/* number of levels in the tree */
	__uint8_t	bc_blocklog;	/* log2(blocksize) of btree blocks */
	xfs_btnum_t	bc_btnum;	/* identifies which btree type */
	union {
		struct {			/* needed for BNO, CNT, INO */
			struct xfs_buf	*agbp;	/* agf/agi buffer pointer */
			struct xfs_defer_ops *dfops;	/* deferred updates */
			xfs_agnumber_t	agno;	/* ag number */
			union xfs_btree_cur_private	priv;
		} a;
		struct {			/* needed for BMAP */
			struct xfs_inode *ip;	/* pointer to our inode */
			struct xfs_defer_ops *dfops;	/* deferred updates */
			xfs_fsblock_t	firstblock;	/* 1st blk allocated */
			int		allocated;	/* count of alloced */
			short		forksize;	/* fork's inode space */
			char		whichfork;	/* data or attr fork */
			char		flags;		/* flags */
#define	XFS_BTCUR_BPRV_WASDEL	1			/* was delayed */
		} b;
	}		bc_private;	/* per-btree type data */
} xfs_btree_cur_t;
#endif	/* __XFS_BTREE_H__ */
