/*
 * Copyright (c) 2000-2001,2005-2006 Silicon Graphics, Inc.
 * Copyright (C) 2010 Red Hat, Inc.
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

#include "libxfs_priv.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include <xfs/xfs_log_format.h>
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode_buf.h"
#include "xfs_inode_fork.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "kmem.h"
static void xfs_trans_free_items(struct xfs_trans *tp);

/*
 * Simple transaction interface
 */

kmem_zone_t	*xfs_log_item_desc_zone;

/*
 * Initialize the precomputed transaction reservation values
 * in the mount structure.
 */
void
xfs_trans_init(
	struct xfs_mount	*mp)
{
	xfs_trans_resv_calc(mp, &mp->m_resv);
}

/*
 * Add the given log item to the transaction's list of log items.
 *
 * The log item will now point to its new descriptor with its li_desc field.
 */
void
libxfs_trans_add_item(
	struct xfs_trans	*tp,
	struct xfs_log_item	*lip)
{
	struct xfs_log_item_desc *lidp;

	lidp = calloc(sizeof(struct xfs_log_item_desc), 1);
	if (!lidp) {
		fprintf(stderr, _("%s: lidp calloc failed (%d bytes): %s\n"),
			progname, (int)sizeof(struct xfs_log_item_desc),
			strerror(errno));
		exit(1);
	}

	lidp->lid_item = lip;
	lidp->lid_flags = 0;
	list_add_tail(&lidp->lid_trans, &tp->t_items);

	lip->li_desc = lidp;
}

/*
 * Unlink and free the given descriptor.
 */
void
libxfs_trans_del_item(
	struct xfs_log_item	*lip)
{
	list_del_init(&lip->li_desc->lid_trans);
	free(lip->li_desc);
	lip->li_desc = NULL;
}

void
libxfs_trans_cancel(
	xfs_trans_t	*tp)
{
#ifdef XACT_DEBUG
	xfs_trans_t	*otp = tp;
#endif
	if (tp != NULL) {
		xfs_trans_free_items(tp);
		free(tp);
		tp = NULL;
	}
#ifdef XACT_DEBUG
	fprintf(stderr, "## cancelled transaction %p\n", otp);
#endif
}


/*
 * This is called to mark the fields indicated in fieldmask as needing
 * to be logged when the transaction is committed.  The inode must
 * already be associated with the given transaction.
 *
 * The values for fieldmask are defined in xfs_log_format.h.  We always
 * log all of the core inode if any of it has changed, and we always log
 * all of the inline data/extents/b-tree root if any of them has changed.
 */
void
xfs_trans_log_inode(
	xfs_trans_t		*tp,
	xfs_inode_t		*ip,
	uint			flags)
{
	ASSERT(ip->i_transp == tp);
	ASSERT(ip->i_itemp != NULL);
#ifdef XACT_DEBUG
	fprintf(stderr, "dirtied inode %llu, transaction %p\n", ip->i_ino, tp);
#endif

	tp->t_flags |= XFS_TRANS_DIRTY;
	ip->i_itemp->ili_item.li_desc->lid_flags |= XFS_LID_DIRTY;

	/*
	 * Always OR in the bits from the ili_last_fields field.
	 * This is to coordinate with the xfs_iflush() and xfs_iflush_done()
	 * routines in the eventual clearing of the ilf_fields bits.
	 * See the big comment in xfs_iflush() for an explanation of
	 * this coordination mechanism.
	 */
	flags |= ip->i_itemp->ili_last_fields;
	ip->i_itemp->ili_fields |= flags;
}

/*
 * This is called to mark bytes first through last inclusive of the given
 * buffer as needing to be logged when the transaction is committed.
 * The buffer must already be associated with the given transaction.
 *
 * First and last are numbers relative to the beginning of this buffer,
 * so the first byte in the buffer is numbered 0 regardless of the
 * value of b_blkno.
 */
void
libxfs_trans_log_buf(
	xfs_trans_t		*tp,
	xfs_buf_t		*bp,
	uint			first,
	uint			last)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);
	ASSERT((first <= last) && (last < XFS_BUF_COUNT(bp)));
#ifdef XACT_DEBUG
	fprintf(stderr, "dirtied buffer %p, transaction %p\n", bp, tp);
#endif

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);

	tp->t_flags |= XFS_TRANS_DIRTY;
	bip->bli_item.li_desc->lid_flags |= XFS_LID_DIRTY;
}

void
libxfs_trans_brelse(
	xfs_trans_t		*tp,
	xfs_buf_t		*bp)
{
	xfs_buf_log_item_t	*bip;
#ifdef XACT_DEBUG
	fprintf(stderr, "released buffer %p, transaction %p\n", bp, tp);
#endif

	if (tp == NULL) {
		ASSERT(XFS_BUF_FSPRIVATE2(bp, void *) == NULL);
		libxfs_putbuf(bp);
		return;
	}
	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(bip->bli_item.li_type == XFS_LI_BUF);
	if (bip->bli_recur > 0) {
		bip->bli_recur--;
		return;
	}
	/* If dirty/stale, can't release till transaction committed */
	if (bip->bli_flags & XFS_BLI_STALE)
		return;
	if (bip->bli_item.li_desc->lid_flags & XFS_LID_DIRTY)
		return;
	xfs_trans_del_item(&bip->bli_item);
	if (bip->bli_flags & XFS_BLI_HOLD)
		bip->bli_flags &= ~XFS_BLI_HOLD;
	XFS_BUF_SET_FSPRIVATE2(bp, NULL);
	libxfs_putbuf(bp);
}

void
libxfs_trans_binval(
	xfs_trans_t		*tp,
	xfs_buf_t		*bp)
{
	xfs_buf_log_item_t	*bip;
#ifdef XACT_DEBUG
	fprintf(stderr, "binval'd buffer %p, transaction %p\n", bp, tp);
#endif

	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	if (bip->bli_flags & XFS_BLI_STALE)
		return;
	XFS_BUF_UNDELAYWRITE(bp);
	xfs_buf_stale(bp);
	bip->bli_flags |= XFS_BLI_STALE;
	bip->bli_flags &= ~XFS_BLI_DIRTY;
	bip->bli_format.blf_flags &= ~XFS_BLF_INODE_BUF;
	bip->bli_format.blf_flags |= XFS_BLF_CANCEL;
	bip->bli_item.li_desc->lid_flags |= XFS_LID_DIRTY;
	tp->t_flags |= XFS_TRANS_DIRTY;
}
static void
buf_item_done(
	xfs_buf_log_item_t	*bip)
{
	xfs_buf_t		*bp;
	int			hold;
	extern kmem_zone_t	*xfs_buf_item_zone;

	bp = bip->bli_buf;
	ASSERT(bp != NULL);
	XFS_BUF_SET_FSPRIVATE(bp, NULL);	/* remove log item */
	XFS_BUF_SET_FSPRIVATE2(bp, NULL);	/* remove xact ptr */

	hold = (bip->bli_flags & XFS_BLI_HOLD);
	if (bip->bli_flags & XFS_BLI_DIRTY) {
#ifdef XACT_DEBUG
		fprintf(stderr, "flushing/staling buffer %p (hold=%d)\n",
			bp, hold);
#endif
		libxfs_writebuf_int(bp, 0);
	}
	if (hold)
		bip->bli_flags &= ~XFS_BLI_HOLD;
	else
		libxfs_putbuf(bp);
	/* release the buf item */
	kmem_zone_free(xfs_buf_item_zone, bip);
}

static void
buf_item_unlock(
	xfs_buf_log_item_t	*bip)
{
	xfs_buf_t		*bp = bip->bli_buf;
	uint			hold;

	/* Clear the buffer's association with this transaction. */
	XFS_BUF_SET_FSPRIVATE2(bip->bli_buf, NULL);

	hold = bip->bli_flags & XFS_BLI_HOLD;
	bip->bli_flags &= ~XFS_BLI_HOLD;
	if (!hold)
		libxfs_putbuf(bp);
}

static void
inode_item_unlock(
	xfs_inode_log_item_t	*iip)
{
	xfs_inode_t		*ip = iip->ili_inode;

	/* Clear the transaction pointer in the inode. */
	ip->i_transp = NULL;

	iip->ili_flags = 0;
}

/*
 * Unlock all of the items of a transaction and free all the descriptors
 * of that transaction.
 */
static void
xfs_trans_free_items(
	struct xfs_trans	*tp)
{
	struct xfs_log_item_desc *lidp, *next;

	list_for_each_entry_safe(lidp, next, &tp->t_items, lid_trans) {
		struct xfs_log_item	*lip = lidp->lid_item;

                xfs_trans_del_item(lip);
		if (lip->li_type == XFS_LI_BUF)
			buf_item_unlock((xfs_buf_log_item_t *)lip);
		else if (lip->li_type == XFS_LI_INODE)
			inode_item_unlock((xfs_inode_log_item_t *)lip);
		else {
			fprintf(stderr, _("%s: unrecognised log item type\n"),
				progname);
			ASSERT(0);
		}
	}
}
