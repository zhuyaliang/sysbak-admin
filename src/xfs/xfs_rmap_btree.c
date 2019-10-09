/*
 * Copyright (c) 2014 Red Hat, Inc.
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
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_ag_resv.h"

STATIC void
xfs_rmapbt_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	int			inc)
{
	struct xfs_buf		*agbp = cur->bc_private.a.agbp;
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agbp);
	xfs_agnumber_t		seqno = be32_to_cpu(agf->agf_seqno);
	int			btnum = cur->bc_btnum;
	struct xfs_perag	*pag = xfs_perag_get(cur->bc_mp, seqno);

	ASSERT(ptr->s != 0);

	agf->agf_roots[btnum] = ptr->s;
	be32_add_cpu(&agf->agf_levels[btnum], inc);
	pag->pagf_levels[btnum] += inc;
	xfs_perag_put(pag);

	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_ROOTS | XFS_AGF_LEVELS);
}

STATIC int
xfs_rmapbt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_rmap_mnr[level != 0];
}

STATIC int
xfs_rmapbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_rmap_mxr[level != 0];
}

STATIC void
xfs_rmapbt_init_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	key->rmap.rm_startblock = rec->rmap.rm_startblock;
	key->rmap.rm_owner = rec->rmap.rm_owner;
	key->rmap.rm_offset = rec->rmap.rm_offset;
}

/*
 * The high key for a reverse mapping record can be computed by shifting
 * the startblock and offset to the highest value that would still map
 * to that record.  In practice this means that we add blockcount-1 to
 * the startblock for all records, and if the record is for a data/attr
 * fork mapping, we add blockcount-1 to the offset too.
 */
STATIC void
xfs_rmapbt_init_high_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	__uint64_t		off;
	int			adj;

	adj = be32_to_cpu(rec->rmap.rm_blockcount) - 1;

	key->rmap.rm_startblock = rec->rmap.rm_startblock;
	be32_add_cpu(&key->rmap.rm_startblock, adj);
	key->rmap.rm_owner = rec->rmap.rm_owner;
	key->rmap.rm_offset = rec->rmap.rm_offset;
	if (XFS_RMAP_NON_INODE_OWNER(be64_to_cpu(rec->rmap.rm_owner)) ||
	    XFS_RMAP_IS_BMBT_BLOCK(be64_to_cpu(rec->rmap.rm_offset)))
		return;
	off = be64_to_cpu(key->rmap.rm_offset);
	off = (XFS_RMAP_OFF(off) + adj) | (off & ~XFS_RMAP_OFF_MASK);
	key->rmap.rm_offset = cpu_to_be64(off);
}

STATIC void
xfs_rmapbt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	rec->rmap.rm_startblock = cpu_to_be32(cur->bc_rec.r.rm_startblock);
	rec->rmap.rm_blockcount = cpu_to_be32(cur->bc_rec.r.rm_blockcount);
	rec->rmap.rm_owner = cpu_to_be64(cur->bc_rec.r.rm_owner);
	rec->rmap.rm_offset = cpu_to_be64(
			xfs_rmap_irec_offset_pack(&cur->bc_rec.r));
}

STATIC void
xfs_rmapbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(cur->bc_private.a.agbp);

	ASSERT(cur->bc_private.a.agno == be32_to_cpu(agf->agf_seqno));
	ASSERT(agf->agf_roots[cur->bc_btnum] != 0);

	ptr->s = agf->agf_roots[cur->bc_btnum];
}

STATIC __int64_t
xfs_rmapbt_key_diff(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	struct xfs_rmap_irec	*rec = &cur->bc_rec.r;
	struct xfs_rmap_key	*kp = &key->rmap;
	__u64			x, y;
	__int64_t		d;

	d = (__int64_t)be32_to_cpu(kp->rm_startblock) - rec->rm_startblock;
	if (d)
		return d;

	x = be64_to_cpu(kp->rm_owner);
	y = rec->rm_owner;
	if (x > y)
		return 1;
	else if (y > x)
		return -1;

	x = XFS_RMAP_OFF(be64_to_cpu(kp->rm_offset));
	y = rec->rm_offset;
	if (x > y)
		return 1;
	else if (y > x)
		return -1;
	return 0;
}

STATIC __int64_t
xfs_rmapbt_diff_two_keys(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	struct xfs_rmap_key	*kp1 = &k1->rmap;
	struct xfs_rmap_key	*kp2 = &k2->rmap;
	__int64_t		d;
	__u64			x, y;

	d = (__int64_t)be32_to_cpu(kp1->rm_startblock) -
		       be32_to_cpu(kp2->rm_startblock);
	if (d)
		return d;

	x = be64_to_cpu(kp1->rm_owner);
	y = be64_to_cpu(kp2->rm_owner);
	if (x > y)
		return 1;
	else if (y > x)
		return -1;

	x = XFS_RMAP_OFF(be64_to_cpu(kp1->rm_offset));
	y = XFS_RMAP_OFF(be64_to_cpu(kp2->rm_offset));
	if (x > y)
		return 1;
	else if (y > x)
		return -1;
	return 0;
}

static bool
xfs_rmapbt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_perag	*pag = bp->b_pag;
	unsigned int		level;

	/*
	 * magic number and level verification
	 *
	 * During growfs operations, we can't verify the exact level or owner as
	 * the perag is not fully initialised and hence not attached to the
	 * buffer.  In this case, check against the maximum tree depth.
	 *
	 * Similarly, during log recovery we will have a perag structure
	 * attached, but the agf information will not yet have been initialised
	 * from the on disk AGF. Again, we can only check against maximum limits
	 * in this case.
	 */
	if (block->bb_magic != cpu_to_be32(XFS_RMAP_CRC_MAGIC))
		return false;

	if (!xfs_sb_version_hasrmapbt(&mp->m_sb))
		return false;
	if (!xfs_btree_sblock_v5hdr_verify(bp))
		return false;

	level = be16_to_cpu(block->bb_level);
	if (pag && pag->pagf_init) {
		if (level >= pag->pagf_levels[XFS_BTNUM_RMAPi])
			return false;
	} else if (level >= mp->m_rmap_maxlevels)
		return false;

	return xfs_btree_sblock_verify(bp, mp->m_rmap_mxr[level != 0]);
}

static void
xfs_rmapbt_read_verify(
	struct xfs_buf	*bp)
{
	if (!xfs_btree_sblock_verify_crc(bp))
		xfs_buf_ioerror(bp, -EFSBADCRC);
	else if (!xfs_rmapbt_verify(bp))
		xfs_buf_ioerror(bp, -EFSCORRUPTED);

	if (bp->b_error) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp);
	}
}

static void
xfs_rmapbt_write_verify(
	struct xfs_buf	*bp)
{
	if (!xfs_rmapbt_verify(bp)) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_buf_ioerror(bp, -EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}
	xfs_btree_sblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_rmapbt_buf_ops = {
	.name			= "xfs_rmapbt",
	.verify_read		= xfs_rmapbt_read_verify,
	.verify_write		= xfs_rmapbt_write_verify,
};

#if defined(DEBUG) || defined(XFS_WARN)
STATIC int
xfs_rmapbt_keys_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	__uint32_t		x;
	__uint32_t		y;
	__uint64_t		a;
	__uint64_t		b;

	x = be32_to_cpu(k1->rmap.rm_startblock);
	y = be32_to_cpu(k2->rmap.rm_startblock);
	if (x < y)
		return 1;
	else if (x > y)
		return 0;
	a = be64_to_cpu(k1->rmap.rm_owner);
	b = be64_to_cpu(k2->rmap.rm_owner);
	if (a < b)
		return 1;
	else if (a > b)
		return 0;
	a = XFS_RMAP_OFF(be64_to_cpu(k1->rmap.rm_offset));
	b = XFS_RMAP_OFF(be64_to_cpu(k2->rmap.rm_offset));
	if (a <= b)
		return 1;
	return 0;
}

STATIC int
xfs_rmapbt_recs_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*r1,
	union xfs_btree_rec	*r2)
{
	__uint32_t		x;
	__uint32_t		y;
	__uint64_t		a;
	__uint64_t		b;

	x = be32_to_cpu(r1->rmap.rm_startblock);
	y = be32_to_cpu(r2->rmap.rm_startblock);
	if (x < y)
		return 1;
	else if (x > y)
		return 0;
	a = be64_to_cpu(r1->rmap.rm_owner);
	b = be64_to_cpu(r2->rmap.rm_owner);
	if (a < b)
		return 1;
	else if (a > b)
		return 0;
	a = XFS_RMAP_OFF(be64_to_cpu(r1->rmap.rm_offset));
	b = XFS_RMAP_OFF(be64_to_cpu(r2->rmap.rm_offset));
	if (a <= b)
		return 1;
	return 0;
}
#endif	/* DEBUG */

/*
 * Calculate number of records in an rmap btree block.
 */
int
xfs_rmapbt_maxrecs(
	struct xfs_mount	*mp,
	int			blocklen,
	int			leaf)
{
	blocklen -= XFS_RMAP_BLOCK_LEN;

	if (leaf)
		return blocklen / sizeof(struct xfs_rmap_rec);
	return blocklen /
		(2 * sizeof(struct xfs_rmap_key) + sizeof(xfs_rmap_ptr_t));
}

/* Compute the maximum height of an rmap btree. */
void
xfs_rmapbt_compute_maxlevels(
	struct xfs_mount		*mp)
{
	/*
	 * On a non-reflink filesystem, the maximum number of rmap
	 * records is the number of blocks in the AG, hence the max
	 * rmapbt height is log_$maxrecs($agblocks).  However, with
	 * reflink each AG block can have up to 2^32 (per the refcount
	 * record format) owners, which means that theoretically we
	 * could face up to 2^64 rmap records.
	 *
	 * That effectively means that the max rmapbt height must be
	 * XFS_BTREE_MAXLEVELS.  "Fortunately" we'll run out of AG
	 * blocks to feed the rmapbt long before the rmapbt reaches
	 * maximum height.  The reflink code uses ag_resv_critical to
	 * disallow reflinking when less than 10% of the per-AG metadata
	 * block reservation since the fallback is a regular file copy.
	 */
	if (xfs_sb_version_hasreflink(&mp->m_sb))
		mp->m_rmap_maxlevels = XFS_BTREE_MAXLEVELS;
	else
		mp->m_rmap_maxlevels = xfs_btree_compute_maxlevels(mp,
				mp->m_rmap_mnr, mp->m_sb.sb_agblocks);
}

/* Calculate the refcount btree size for some records. */
xfs_extlen_t
xfs_rmapbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(mp, mp->m_rmap_mnr, len);
}

/*
 * Calculate the maximum refcount btree size.
 */
xfs_extlen_t
xfs_rmapbt_max_size(
	struct xfs_mount	*mp)
{
	/* Bail out if we're uninitialized, which can happen in mkfs. */
	if (mp->m_rmap_mxr[0] == 0)
		return 0;

	return xfs_rmapbt_calc_size(mp, mp->m_sb.sb_agblocks);
}
