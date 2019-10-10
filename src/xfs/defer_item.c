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
#include "xfs_fs.h"
#include <xfs/xfs_format.h>
#include <xfs/xfs_log_format.h>
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_rmap.h"
#include "xfs_inode.h"

enum xfs_refcount_intent_type {
    XFS_REFCOUNT_INCREASE = 1,
    XFS_REFCOUNT_DECREASE,
    XFS_REFCOUNT_ALLOC_COW,
    XFS_REFCOUNT_FREE_COW,
};

struct xfs_refcount_intent {
    struct list_head            ri_list;
    enum xfs_refcount_intent_type       ri_type;
    xfs_fsblock_t               ri_startblock;
    xfs_extlen_t                ri_blockcount;
};

#define container_of(ptr, type, member) ({          \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})


static const struct xfs_defer_op_type *defer_op_types[XFS_DEFER_OPS_TYPE_MAX];
static int
xfs_extent_free_diff_items(
	void				*priv,
	struct list_head		*a,
	struct list_head		*b)
{
	struct xfs_mount		*mp = priv;
	struct xfs_extent_free_item	*ra;
	struct xfs_extent_free_item	*rb;

	ra = container_of(a, struct xfs_extent_free_item, xefi_list);
	rb = container_of(b, struct xfs_extent_free_item, xefi_list);
	return  XFS_FSB_TO_AGNO(mp, ra->xefi_startblock) -
		XFS_FSB_TO_AGNO(mp, rb->xefi_startblock);
}
STATIC void *
xfs_extent_free_create_intent(
	struct xfs_trans		*tp,
	unsigned int			count)
{
	return NULL;
}

STATIC void *
xfs_extent_free_create_done(
	struct xfs_trans		*tp,
	void				*intent,
	unsigned int			count)
{
	return NULL;
}
STATIC void
xfs_extent_free_cancel_item(
	struct list_head		*item)
{
	struct xfs_extent_free_item	*xfsfree;

	xfsfree = container_of(item, struct xfs_extent_free_item, xefi_list);
	free(xfsfree);
}
static const struct xfs_defer_op_type xfs_extent_free_defer_type = {
	.type		= XFS_DEFER_OPS_TYPE_FREE,
	.diff_items	= xfs_extent_free_diff_items,
	.create_intent	= xfs_extent_free_create_intent,
	.create_done	= xfs_extent_free_create_done,
	.cancel_item	= xfs_extent_free_cancel_item,
};

static void
xfs_defer_init_op_type(
	const struct xfs_defer_op_type	*type)
{
	defer_op_types[type->type] = type;
}

void xfs_extent_free_init_defer_op(void)
{
	xfs_defer_init_op_type(&xfs_extent_free_defer_type);
}
static int
xfs_rmap_update_diff_items(
	void				*priv,
	struct list_head		*a,
	struct list_head		*b)
{
	struct xfs_mount		*mp = priv;
	struct xfs_rmap_intent		*ra;
	struct xfs_rmap_intent		*rb;

	ra = container_of(a, struct xfs_rmap_intent, ri_list);
	rb = container_of(b, struct xfs_rmap_intent, ri_list);
	return  XFS_FSB_TO_AGNO(mp, ra->ri_bmap.br_startblock) -
		XFS_FSB_TO_AGNO(mp, rb->ri_bmap.br_startblock);
}

STATIC void *
xfs_rmap_update_create_intent(
	struct xfs_trans		*tp,
	unsigned int			count)
{
	return NULL;
}
STATIC void *
xfs_rmap_update_create_done(
	struct xfs_trans		*tp,
	void				*intent,
	unsigned int			count)
{
	return NULL;
}
STATIC void
xfs_rmap_update_cancel_item(
	struct list_head		*item)
{
	struct xfs_rmap_intent		*rmap;

	rmap = container_of(item, struct xfs_rmap_intent, ri_list);
	free(rmap);
}

static const struct xfs_defer_op_type xfs_rmap_update_defer_type = {
	.type		= XFS_DEFER_OPS_TYPE_RMAP,
	.diff_items	= xfs_rmap_update_diff_items,
	.create_intent	= xfs_rmap_update_create_intent,
	.create_done	= xfs_rmap_update_create_done,
	.cancel_item	= xfs_rmap_update_cancel_item,
};

void xfs_rmap_update_init_defer_op(void)
{
	xfs_defer_init_op_type(&xfs_rmap_update_defer_type);
}
static int
xfs_refcount_update_diff_items(
	void				*priv,
	struct list_head		*a,
	struct list_head		*b)
{
	struct xfs_mount		*mp = priv;
	struct xfs_refcount_intent	*ra;
	struct xfs_refcount_intent	*rb;

	ra = container_of(a, struct xfs_refcount_intent, ri_list);
	rb = container_of(b, struct xfs_refcount_intent, ri_list);
	return  XFS_FSB_TO_AGNO(mp, ra->ri_startblock) -
		XFS_FSB_TO_AGNO(mp, rb->ri_startblock);
}

STATIC void *
xfs_refcount_update_create_intent(
	struct xfs_trans		*tp,
	unsigned int			count)
{
	return NULL;
}

STATIC void *
xfs_refcount_update_create_done(
	struct xfs_trans		*tp,
	void				*intent,
	unsigned int			count)
{
	return NULL;
}
STATIC void
xfs_refcount_update_cancel_item(
	struct list_head		*item)
{
	struct xfs_refcount_intent	*refc;

	refc = container_of(item, struct xfs_refcount_intent, ri_list);
	free(refc);
}

static const struct xfs_defer_op_type xfs_refcount_update_defer_type = {
	.type		= XFS_DEFER_OPS_TYPE_REFCOUNT,
	.diff_items	= xfs_refcount_update_diff_items,
	.create_intent	= xfs_refcount_update_create_intent,
	.create_done	= xfs_refcount_update_create_done,
	.cancel_item	= xfs_refcount_update_cancel_item,
};
void xfs_refcount_update_init_defer_op(void)
{
	xfs_defer_init_op_type(&xfs_refcount_update_defer_type);
}

static int
xfs_bmap_update_diff_items(
	void				*priv,
	struct list_head		*a,
	struct list_head		*b)
{
	struct xfs_bmap_intent		*ba;
	struct xfs_bmap_intent		*bb;

	ba = container_of(a, struct xfs_bmap_intent, bi_list);
	bb = container_of(b, struct xfs_bmap_intent, bi_list);
	return ba->bi_owner->i_ino - bb->bi_owner->i_ino;
}
STATIC void *
xfs_bmap_update_create_intent(
	struct xfs_trans		*tp,
	unsigned int			count)
{
	return NULL;
}

STATIC void *
xfs_bmap_update_create_done(
	struct xfs_trans		*tp,
	void				*intent,
	unsigned int			count)
{
	return NULL;
}
STATIC void
xfs_bmap_update_cancel_item(
	struct list_head		*item)
{
	struct xfs_bmap_intent		*bmap;

	bmap = container_of(item, struct xfs_bmap_intent, bi_list);
	free(bmap);
}

static const struct xfs_defer_op_type xfs_bmap_update_defer_type = {
	.type		= XFS_DEFER_OPS_TYPE_BMAP,
	.diff_items	= xfs_bmap_update_diff_items,
	.create_intent	= xfs_bmap_update_create_intent,
	.create_done	= xfs_bmap_update_create_done,
	.cancel_item	= xfs_bmap_update_cancel_item,
};
void xfs_bmap_update_init_defer_op(void)
{
	xfs_defer_init_op_type(&xfs_bmap_update_defer_type);
}
