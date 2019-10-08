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
#include <xfs/xfs_fs.h>
#include "xfs_shared.h"
#include <xfs/xfs_format.h>
#include <xfs/xfs_log_format.h>
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include <xfs/xfs_da_format.h>
#include "xfs_da_btree.h"
#include "xfs_btree.h"
#include "xfs_trans.h"
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_trace.h"
#include "xfs_bmap.h"
#include "xfs_inode.h"

/*
 * Lookup the first record less than or equal to [bno, len, owner, offset]
 * in the btree given by cur.
 */
int
xfs_rmap_lookup_le(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags,
	int			*stat)
{
	cur->bc_rec.r.rm_startblock = bno;
	cur->bc_rec.r.rm_blockcount = len;
	cur->bc_rec.r.rm_owner = owner;
	cur->bc_rec.r.rm_offset = offset;
	cur->bc_rec.r.rm_flags = flags;
	return xfs_btree_lookup(cur, XFS_LOOKUP_LE, stat);
}

/*
 * Lookup the record exactly matching [bno, len, owner, offset]
 * in the btree given by cur.
 */
int
xfs_rmap_lookup_eq(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags,
	int			*stat)
{
	cur->bc_rec.r.rm_startblock = bno;
	cur->bc_rec.r.rm_blockcount = len;
	cur->bc_rec.r.rm_owner = owner;
	cur->bc_rec.r.rm_offset = offset;
	cur->bc_rec.r.rm_flags = flags;
	return xfs_btree_lookup(cur, XFS_LOOKUP_EQ, stat);
}

/*
 * Update the record referred to by cur to the value given
 * by [bno, len, owner, offset].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_rmap_update(
	struct xfs_btree_cur	*cur,
	struct xfs_rmap_irec	*irec)
{
	union xfs_btree_rec	rec;
	int			error;

	trace_xfs_rmap_update(cur->bc_mp, cur->bc_private.a.agno,
			irec->rm_startblock, irec->rm_blockcount,
			irec->rm_owner, irec->rm_offset, irec->rm_flags);

	rec.rmap.rm_startblock = cpu_to_be32(irec->rm_startblock);
	rec.rmap.rm_blockcount = cpu_to_be32(irec->rm_blockcount);
	rec.rmap.rm_owner = cpu_to_be64(irec->rm_owner);
	rec.rmap.rm_offset = cpu_to_be64(
			xfs_rmap_irec_offset_pack(irec));
	error = xfs_btree_update(cur, &rec);
	if (error)
		trace_xfs_rmap_update_error(cur->bc_mp,
				cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}


static int
xfs_rmap_btrec_to_irec(
	union xfs_btree_rec	*rec,
	struct xfs_rmap_irec	*irec)
{
	irec->rm_flags = 0;
	irec->rm_startblock = be32_to_cpu(rec->rmap.rm_startblock);
	irec->rm_blockcount = be32_to_cpu(rec->rmap.rm_blockcount);
	irec->rm_owner = be64_to_cpu(rec->rmap.rm_owner);
	return xfs_rmap_irec_offset_unpack(be64_to_cpu(rec->rmap.rm_offset),
			irec);
}

struct xfs_find_left_neighbor_info {
	struct xfs_rmap_irec	high;
	struct xfs_rmap_irec	*irec;
	int			*stat;
};


/*
 * A mergeable rmap must have the same owner and the same values for
 * the unwritten, attr_fork, and bmbt flags.  The startblock and
 * offset are checked separately.
 */
static bool
xfs_rmap_is_mergeable(
	struct xfs_rmap_irec	*irec,
	uint64_t		owner,
	unsigned int		flags)
{
	if (irec->rm_owner == XFS_RMAP_OWN_NULL)
		return false;
	if (irec->rm_owner != owner)
		return false;
	if ((flags & XFS_RMAP_UNWRITTEN) ^
	    (irec->rm_flags & XFS_RMAP_UNWRITTEN))
		return false;
	if ((flags & XFS_RMAP_ATTR_FORK) ^
	    (irec->rm_flags & XFS_RMAP_ATTR_FORK))
		return false;
	if ((flags & XFS_RMAP_BMBT_BLOCK) ^
	    (irec->rm_flags & XFS_RMAP_BMBT_BLOCK))
		return false;
	return true;
}

#define RMAP_LEFT_CONTIG	(1 << 0)
#define RMAP_RIGHT_CONTIG	(1 << 1)
#define RMAP_LEFT_FILLING	(1 << 2)
#define RMAP_RIGHT_FILLING	(1 << 3)
#define RMAP_LEFT_VALID		(1 << 6)
#define RMAP_RIGHT_VALID	(1 << 7)

#define LEFT		r[0]
#define RIGHT		r[1]
#define PREV		r[2]
#define NEW		r[3]


#undef	NEW
#undef	LEFT
#undef	RIGHT
#undef	PREV


struct xfs_rmap_query_range_info {
	xfs_rmap_query_range_fn	fn;
	void				*priv;
};


static bool
xfs_rmap_update_is_needed(
	struct xfs_mount	*mp,
	int			whichfork)
{
	return xfs_sb_version_hasrmapbt(&mp->m_sb) && whichfork != XFS_COW_FORK;
}

/*
 * Record a rmap intent; the list is kept sorted first by AG and then by
 * increasing age.
 */
static int
__xfs_rmap_add(
	struct xfs_mount		*mp,
	struct xfs_defer_ops		*dfops,
	enum xfs_rmap_intent_type	type,
	__uint64_t			owner,
	int				whichfork,
	struct xfs_bmbt_irec1		*bmap)
{
	struct xfs_rmap_intent	*ri;

	trace_xfs_rmap_defer(mp, XFS_FSB_TO_AGNO(mp, bmap->br_startblock),
			type,
			XFS_FSB_TO_AGBNO(mp, bmap->br_startblock),
			owner, whichfork,
			bmap->br_startoff,
			bmap->br_blockcount,
			bmap->br_state);

	ri = kmem_alloc(sizeof(struct xfs_rmap_intent), KM_SLEEP | KM_NOFS);
	INIT_LIST_HEAD(&ri->ri_list);
	ri->ri_type = type;
	ri->ri_owner = owner;
	ri->ri_whichfork = whichfork;
	ri->ri_bmap = *bmap;

	xfs_defer_add(dfops, XFS_DEFER_OPS_TYPE_RMAP, &ri->ri_list);
	return 0;
}
