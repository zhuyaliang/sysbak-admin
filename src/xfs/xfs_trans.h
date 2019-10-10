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

#ifndef __XFS_TRANS_H__
#define __XFS_TRANS_H__
#include <xfs/xfs_log_format.h>
struct xfs_mount;
struct xfs_buftarg;
struct xfs_buf;
struct xfs_buf_map;


typedef struct xfs_log_item {
	struct xfs_log_item_desc	*li_desc;	/* ptr to current desc*/
	struct xfs_mount		*li_mountp;	/* ptr to fs mount */
	uint				li_type;	/* item type */
	xfs_lsn_t			li_lsn;
} xfs_log_item_t;

typedef struct xfs_inode_log_item {
	xfs_log_item_t		ili_item;		/* common portion */
	struct xfs_inode	*ili_inode;		/* inode pointer */
	unsigned short		ili_flags;		/* misc flags */
	unsigned int		ili_fields;		/* fields to be logged */
	unsigned int		ili_last_fields;	/* fields when flushed*/
	struct xfs_inode_log_format	ili_format;		/* logged structure */
} xfs_inode_log_item_t;

typedef struct xfs_buf_log_item {
	xfs_log_item_t		bli_item;	/* common item structure */
	struct xfs_buf		*bli_buf;	/* real buffer pointer */
	unsigned int		bli_flags;	/* misc flags */
	unsigned int		bli_recur;	/* recursion count */
	xfs_buf_log_format_t	bli_format;	/* in-log header */
} xfs_buf_log_item_t;

#define XFS_BLI_DIRTY			(1<<0)
#define XFS_BLI_HOLD			(1<<1)
#define XFS_BLI_STALE			(1<<2)
#define XFS_BLI_INODE_ALLOC_BUF		(1<<3)

typedef struct xfs_dq_logitem {
	xfs_log_item_t		qli_item;	/* common portion */
	struct xfs_dquot	*qli_dquot;	/* dquot ptr */
	xfs_lsn_t		qli_flush_lsn;	/* lsn at last flush */
	xfs_dq_logformat_t	qli_format;	/* logged structure */
} xfs_dq_logitem_t;

typedef struct xfs_qoff_logitem {
	xfs_log_item_t		qql_item;	/* common portion */
	struct xfs_qoff_logitem	*qql_start_lip;	/* qoff-start logitem, if any */
	xfs_qoff_logformat_t	qql_format;	/* logged structure */
} xfs_qoff_logitem_t;

typedef struct xfs_trans {
	unsigned int	t_type;			/* transaction type */
	unsigned int	t_log_res;		/* amt of log space resvd */
	unsigned int	t_log_count;		/* count for perm log res */
	unsigned int	t_blk_res;		/* # of blocks resvd */
	struct xfs_mount *t_mountp;		/* ptr to fs mount struct */
	unsigned int	t_flags;		/* misc flags */
	long		t_icount_delta;		/* superblock icount change */
	long		t_ifree_delta;		/* superblock ifree change */
	long		t_fdblocks_delta;	/* superblock fdblocks chg */
	long		t_frextents_delta;	/* superblock freextents chg */
	struct list_head	t_items;	/* first log item desc chunk */
} xfs_trans_t;

void	libxfs_trans_cancel(struct xfs_trans *);
void	libxfs_trans_log_inode (struct xfs_trans *, struct xfs_inode *,
				uint);

void	libxfs_trans_brelse(struct xfs_trans *, struct xfs_buf *);
void	libxfs_trans_binval(struct xfs_trans *, struct xfs_buf *);
void	libxfs_trans_log_buf(struct xfs_trans *, struct xfs_buf *,
				uint, uint);
void xfs_extent_free_init_defer_op(void);
void xfs_rmap_update_init_defer_op(void);
void xfs_refcount_update_init_defer_op(void);
void xfs_bmap_update_init_defer_op(void);

#endif	/* __XFS_TRANS_H__ */
