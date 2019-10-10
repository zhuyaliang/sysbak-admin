/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef __XFS_DEFER_H__
#define	__XFS_DEFER_H__

struct xfs_defer_op_type;

enum xfs_defer_ops_type {
	XFS_DEFER_OPS_TYPE_BMAP,
	XFS_DEFER_OPS_TYPE_REFCOUNT,
	XFS_DEFER_OPS_TYPE_RMAP,
	XFS_DEFER_OPS_TYPE_FREE,
	XFS_DEFER_OPS_TYPE_MAX,
};

#define XFS_DEFER_OPS_NR_INODES	2	/* join up to two inodes */

struct xfs_defer_ops {
	bool			dop_committed;	/* did any trans commit? */
	bool			dop_low;	/* alloc in low mode */
	struct list_head	dop_intake;	/* unlogged pending work */
	struct list_head	dop_pending;	/* logged pending work */
	struct xfs_inode	*dop_inodes[XFS_DEFER_OPS_NR_INODES];
};

void xfs_extent_free_init_defer_op(void);
void xfs_rmap_update_init_defer_op(void);
void xfs_bmap_update_init_defer_op(void);
void xfs_refcount_update_init_defer_op(void);
struct xfs_defer_op_type {
	enum xfs_defer_ops_type	type;
	unsigned int		max_items;
	void (*abort_intent)(void *);
	void *(*create_done)(struct xfs_trans *, void *, unsigned int);
	void (*cancel_item)(struct list_head *);
	int (*diff_items)(void *, struct list_head *, struct list_head *);
	void *(*create_intent)(struct xfs_trans *, uint);
	void (*log_item)(struct xfs_trans *, void *, struct list_head *);
};

#endif /* __XFS_DEFER_H__ */
