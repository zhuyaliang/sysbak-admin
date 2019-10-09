/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#include "xfs_format.h"
#include <xfs/xfs_log_format.h>
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_rmap.h"
#include "xfs_alloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_cksum.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_ag_resv.h"

struct workqueue_struct *xfs_alloc_wq;

#define XFS_ABSDIFF(a,b)	(((a) <= (b)) ? ((b) - (a)) : ((a) - (b)))

#define	XFSA_FIXUP_BNO_OK	1
#define	XFSA_FIXUP_CNT_OK	2

unsigned int
xfs_refc_block(
	struct xfs_mount	*mp)
{
	if (xfs_sb_version_hasrmapbt(&mp->m_sb))
		return XFS_RMAP_BLOCK(mp) + 1;
	if (xfs_sb_version_hasfinobt(&mp->m_sb))
		return XFS_FIBT_BLOCK(mp) + 1;
	return XFS_IBT_BLOCK(mp) + 1;
}

xfs_extlen_t
xfs_prealloc_blocks(
	struct xfs_mount	*mp)
{
	if (xfs_sb_version_hasreflink(&mp->m_sb))
		return xfs_refc_block(mp) + 1;
	if (xfs_sb_version_hasrmapbt(&mp->m_sb))
		return XFS_RMAP_BLOCK(mp) + 1;
	if (xfs_sb_version_hasfinobt(&mp->m_sb))
		return XFS_FIBT_BLOCK(mp) + 1;
	return XFS_IBT_BLOCK(mp) + 1;
}

/*
 * In order to avoid ENOSPC-related deadlock caused by out-of-order locking of
 * AGF buffer (PV 947395), we place constraints on the relationship among
 * actual allocations for data blocks, freelist blocks, and potential file data
 * bmap btree blocks. However, these restrictions may result in no actual space
 * allocated for a delayed extent, for example, a data block in a certain AG is
 * allocated but there is no additional block for the additional bmap btree
 * block due to a split of the bmap btree of the file. The result of this may
 * lead to an infinite loop when the file gets flushed to disk and all delayed
 * extents need to be actually allocated. To get around this, we explicitly set
 * aside a few blocks which will not be reserved in delayed allocation.
 *
 * We need to reserve 4 fsbs _per AG_ for the freelist and 4 more to handle a
 * potential split of the file's bmap btree.
 */
unsigned int
xfs_alloc_set_aside(
	struct xfs_mount	*mp)
{
	unsigned int		blocks;

	blocks = 4 + (mp->m_sb.sb_agcount * XFS_ALLOC_AGFL_RESERVE);
	return blocks;
}

/*
 * When deciding how much space to allocate out of an AG, we limit the
 * allocation maximum size to the size the AG. However, we cannot use all the
 * blocks in the AG - some are permanently used by metadata. These
 * blocks are generally:
 *	- the AG superblock, AGF, AGI and AGFL
 *	- the AGF (bno and cnt) and AGI btree root blocks, and optionally
 *	  the AGI free inode and rmap btree root blocks.
 *	- blocks on the AGFL according to xfs_alloc_set_aside() limits
 *	- the rmapbt root block
 *
 * The AG headers are sector sized, so the amount of space they take up is
 * dependent on filesystem geometry. The others are all single blocks.
 */
unsigned int
xfs_alloc_ag_max_usable(
	struct xfs_mount	*mp)
{
	unsigned int		blocks;

	blocks = XFS_BB_TO_FSB(mp, XFS_FSS_TO_BB(mp, 4)); /* ag headers */
	blocks += XFS_ALLOC_AGFL_RESERVE;
	blocks += 3;			/* AGF, AGI btree root blocks */
	if (xfs_sb_version_hasfinobt(&mp->m_sb))
		blocks++;		/* finobt root block */
	if (xfs_sb_version_hasrmapbt(&mp->m_sb))
		blocks++; 		/* rmap root block */
	if (xfs_sb_version_hasreflink(&mp->m_sb))
		blocks++;		/* refcount root block */

	return mp->m_sb.sb_agblocks - blocks;
}

static bool
xfs_agfl_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_agfl	*agfl = XFS_BUF_TO_AGFL(bp);
	int		i;

	if (!uuid_equal(&agfl->agfl_uuid, &mp->m_sb.sb_meta_uuid))
		return false;
	if (be32_to_cpu(agfl->agfl_magicnum) != XFS_AGFL_MAGIC)
		return false;
	/*
	 * during growfs operations, the perag is not fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag && be32_to_cpu(agfl->agfl_seqno) != bp->b_pag->pag_agno)
		return false;

	for (i = 0; i < XFS_AGFL_SIZE(mp); i++) {
		if (be32_to_cpu(agfl->agfl_bno[i]) != NULLAGBLOCK &&
		    be32_to_cpu(agfl->agfl_bno[i]) >= mp->m_sb.sb_agblocks)
			return false;
	}

	return xfs_log_check_lsn(mp,
				 be64_to_cpu(XFS_BUF_TO_AGFL(bp)->agfl_lsn));
}

static void
xfs_agfl_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;

	/*
	 * There is no verification of non-crc AGFLs because mkfs does not
	 * initialise the AGFL to zero or NULL. Hence the only valid part of the
	 * AGFL is what the AGF says is active. We can't get to the AGF, so we
	 * can't verify just those entries are valid.
	 */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (!xfs_buf_verify_cksum(bp, XFS_AGFL_CRC_OFF))
		xfs_buf_ioerror(bp, -EFSBADCRC);
	else if (!xfs_agfl_verify(bp))
		xfs_buf_ioerror(bp, -EFSCORRUPTED);

	if (bp->b_error)
		xfs_verifier_error(bp);
}

static void
xfs_agfl_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_buf_log_item	*bip = bp->b_fspriv;

	/* no verification of non-crc AGFLs */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (!xfs_agfl_verify(bp)) {
		xfs_buf_ioerror(bp, -EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}

	if (bip)
		XFS_BUF_TO_AGFL(bp)->agfl_lsn = cpu_to_be64(bip->bli_item.li_lsn);

	xfs_buf_update_cksum(bp, XFS_AGFL_CRC_OFF);
}

const struct xfs_buf_ops xfs_agfl_buf_ops = {
	.name = "xfs_agfl",
	.verify_read = xfs_agfl_read_verify,
	.verify_write = xfs_agfl_write_verify,
};

STATIC int
xfs_alloc_update_counters(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_buf		*agbp,
	long			len)
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agbp);

	pag->pagf_freeblks += len;
	be32_add_cpu(&agf->agf_freeblks, len);

	xfs_trans_agblocks_delta(tp, len);
	if (unlikely(be32_to_cpu(agf->agf_freeblks) >
		     be32_to_cpu(agf->agf_length)))
		return -EFSCORRUPTED;

	xfs_alloc_log_agf(tp, agbp, XFS_AGF_FREEBLKS);
	return 0;
}

unsigned int
xfs_alloc_min_freelist(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag)
{
	unsigned int		min_free;

	/* space needed by-bno freespace btree */
	min_free = min_t(unsigned int, pag->pagf_levels[XFS_BTNUM_BNOi] + 1,
				       mp->m_ag_maxlevels);
	/* space needed by-size freespace btree */
	min_free += min_t(unsigned int, pag->pagf_levels[XFS_BTNUM_CNTi] + 1,
				       mp->m_ag_maxlevels);
	/* space needed reverse mapping used space btree */
	if (xfs_sb_version_hasrmapbt(&mp->m_sb))
		min_free += min_t(unsigned int,
				  pag->pagf_levels[XFS_BTNUM_RMAPi] + 1,
				  mp->m_rmap_maxlevels);

	return min_free;
}

/*
 * Log the given fields from the agf structure.
 */
void
xfs_alloc_log_agf(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_buf_t	*bp,	/* buffer for a.g. freelist header */
	int		fields)	/* mask of fields to be logged (XFS_AGF_...) */
{
	int	first;		/* first byte offset */
	int	last;		/* last byte offset */
	static const short	offsets[] = {
		offsetof(xfs_agf_t, agf_magicnum),
		offsetof(xfs_agf_t, agf_versionnum),
		offsetof(xfs_agf_t, agf_seqno),
		offsetof(xfs_agf_t, agf_length),
		offsetof(xfs_agf_t, agf_roots[0]),
		offsetof(xfs_agf_t, agf_levels[0]),
		offsetof(xfs_agf_t, agf_flfirst),
		offsetof(xfs_agf_t, agf_fllast),
		offsetof(xfs_agf_t, agf_flcount),
		offsetof(xfs_agf_t, agf_freeblks),
		offsetof(xfs_agf_t, agf_longest),
		offsetof(xfs_agf_t, agf_btreeblks),
		offsetof(xfs_agf_t, agf_uuid),
		offsetof(xfs_agf_t, agf_rmap_blocks),
		offsetof(xfs_agf_t, agf_refcount_blocks),
		offsetof(xfs_agf_t, agf_refcount_root),
		offsetof(xfs_agf_t, agf_refcount_level),
		/* needed so that we don't log the whole rest of the structure: */
		offsetof(xfs_agf_t, agf_spare64),
		sizeof(xfs_agf_t)
	};

	trace_xfs_agf(tp->t_mountp, XFS_BUF_TO_AGF(bp), fields, _RET_IP_);

	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_AGF_BUF);

	xfs_btree_offsets(fields, offsets, XFS_AGF_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, bp, (uint)first, (uint)last);
}
static bool
xfs_agf_verify(
	struct xfs_mount *mp,
	struct xfs_buf	*bp)
 {
	struct xfs_agf	*agf = XFS_BUF_TO_AGF(bp);

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		if (!uuid_equal(&agf->agf_uuid, &mp->m_sb.sb_meta_uuid))
			return false;
		if (!xfs_log_check_lsn(mp,
				be64_to_cpu(XFS_BUF_TO_AGF(bp)->agf_lsn)))
			return false;
	}

	if (!(agf->agf_magicnum == cpu_to_be32(XFS_AGF_MAGIC) &&
	      XFS_AGF_GOOD_VERSION(be32_to_cpu(agf->agf_versionnum)) &&
	      be32_to_cpu(agf->agf_freeblks) <= be32_to_cpu(agf->agf_length) &&
	      be32_to_cpu(agf->agf_flfirst) < XFS_AGFL_SIZE(mp) &&
	      be32_to_cpu(agf->agf_fllast) < XFS_AGFL_SIZE(mp) &&
	      be32_to_cpu(agf->agf_flcount) <= XFS_AGFL_SIZE(mp)))
		return false;

	if (be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]) > XFS_BTREE_MAXLEVELS ||
	    be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNT]) > XFS_BTREE_MAXLEVELS)
		return false;

	if (xfs_sb_version_hasrmapbt(&mp->m_sb) &&
	    be32_to_cpu(agf->agf_levels[XFS_BTNUM_RMAP]) > XFS_BTREE_MAXLEVELS)
		return false;

	/*
	 * during growfs operations, the perag is not fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag && be32_to_cpu(agf->agf_seqno) != bp->b_pag->pag_agno)
		return false;

	if (xfs_sb_version_haslazysbcount(&mp->m_sb) &&
	    be32_to_cpu(agf->agf_btreeblks) > be32_to_cpu(agf->agf_length))
		return false;

	if (xfs_sb_version_hasreflink(&mp->m_sb) &&
	    be32_to_cpu(agf->agf_refcount_level) > XFS_BTREE_MAXLEVELS)
		return false;

	return true;;

}

static void
xfs_agf_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;

	if (xfs_sb_version_hascrc(&mp->m_sb) &&
	    !xfs_buf_verify_cksum(bp, XFS_AGF_CRC_OFF))
		xfs_buf_ioerror(bp, -EFSBADCRC);
	else if (XFS_TEST_ERROR(!xfs_agf_verify(mp, bp), mp,
				XFS_ERRTAG_ALLOC_READ_AGF,
				XFS_RANDOM_ALLOC_READ_AGF))
		xfs_buf_ioerror(bp, -EFSCORRUPTED);

	if (bp->b_error)
		xfs_verifier_error(bp);
}

static void
xfs_agf_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_buf_log_item	*bip = bp->b_fspriv;

	if (!xfs_agf_verify(mp, bp)) {
		xfs_buf_ioerror(bp, -EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (bip)
		XFS_BUF_TO_AGF(bp)->agf_lsn = cpu_to_be64(bip->bli_item.li_lsn);

	xfs_buf_update_cksum(bp, XFS_AGF_CRC_OFF);
}

const struct xfs_buf_ops xfs_agf_buf_ops = {
	.name = "xfs_agf",
	.verify_read = xfs_agf_read_verify,
	.verify_write = xfs_agf_write_verify,
};
