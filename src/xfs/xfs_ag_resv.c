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
#include "libxfs_priv.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include <xfs/xfs_format.h>
#include <xfs/xfs_log_format.h>
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_alloc.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"
#include "xfs_bit.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_ag_resv.h"
#include "xfs_trans_space.h"
#include "xfs_rmap_btree.h"
#include "xfs_btree.h"

bool
xfs_ag_resv_critical(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type)
{
	xfs_extlen_t			avail;
	xfs_extlen_t			orig;

	switch (type) {
	case XFS_AG_RESV_METADATA:
		avail = pag->pagf_freeblks - pag->pag_agfl_resv.ar_reserved;
		orig = pag->pag_meta_resv.ar_asked;
		break;
	case XFS_AG_RESV_AGFL:
		avail = pag->pagf_freeblks + pag->pagf_flcount -
			pag->pag_meta_resv.ar_reserved;
		orig = pag->pag_agfl_resv.ar_asked;
		break;
	default:
		ASSERT(0);
		return false;
	}

	trace_xfs_ag_resv_critical(pag, type, avail);

	/* Critically low if less than 10% or max btree height remains. */
	return XFS_TEST_ERROR(avail < orig / 10 || avail < XFS_BTREE_MAXLEVELS,
			pag->pag_mount, XFS_ERRTAG_AG_RESV_CRITICAL,
			XFS_RANDOM_AG_RESV_CRITICAL);
}

/*
 * How many blocks are reserved but not used, and therefore must not be
 * allocated away?
 */
xfs_extlen_t
xfs_ag_resv_needed(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type)
{
	xfs_extlen_t			len;

	len = pag->pag_meta_resv.ar_reserved + pag->pag_agfl_resv.ar_reserved;
	switch (type) {
	case XFS_AG_RESV_METADATA:
	case XFS_AG_RESV_AGFL:
		len -= xfs_perag_resv(pag, type)->ar_reserved;
		break;
	case XFS_AG_RESV_NONE:
		/* empty */
		break;
	default:
		ASSERT(0);
	}

	trace_xfs_ag_resv_needed(pag, type, len);

	return len;
}


/* Allocate a block from the reservation. */
void
xfs_ag_resv_alloc_extent(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type,
	struct xfs_alloc_arg		*args)
{
	struct xfs_ag_resv		*resv;
	xfs_extlen_t			len;
	uint				field;

	trace_xfs_ag_resv_alloc_extent(pag, type, args->len);

	switch (type) {
	case XFS_AG_RESV_METADATA:
	case XFS_AG_RESV_AGFL:
		resv = xfs_perag_resv(pag, type);
		break;
	default:
		ASSERT(0);
		/* fall through */
	case XFS_AG_RESV_NONE:
		field = args->wasdel ? XFS_TRANS_SB_RES_FDBLOCKS :
				       XFS_TRANS_SB_FDBLOCKS;
		xfs_trans_mod_sb(args->tp, field, -(int64_t)args->len);
		return;
	}

	len = min_t(xfs_extlen_t, args->len, resv->ar_reserved);
	resv->ar_reserved -= len;
	if (type == XFS_AG_RESV_AGFL)
		return;
	/* Allocations of reserved blocks only need on-disk sb updates... */
	xfs_trans_mod_sb(args->tp, XFS_TRANS_SB_RES_FDBLOCKS, -(int64_t)len);
	/* ...but non-reserved blocks need in-core and on-disk updates. */
	if (args->len > len)
		xfs_trans_mod_sb(args->tp, XFS_TRANS_SB_FDBLOCKS,
				-((int64_t)args->len - len));
}

/* Free a block to the reservation. */
void
xfs_ag_resv_free_extent(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type,
	struct xfs_trans		*tp,
	xfs_extlen_t			len)
{
	xfs_extlen_t			leftover;
	struct xfs_ag_resv		*resv;

	trace_xfs_ag_resv_free_extent(pag, type, len);

	switch (type) {
	case XFS_AG_RESV_METADATA:
	case XFS_AG_RESV_AGFL:
		resv = xfs_perag_resv(pag, type);
		break;
	default:
		ASSERT(0);
		/* fall through */
	case XFS_AG_RESV_NONE:
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, (int64_t)len);
		return;
	}

	leftover = min_t(xfs_extlen_t, len, resv->ar_asked - resv->ar_reserved);
	resv->ar_reserved += leftover;
	if (type == XFS_AG_RESV_AGFL)
		return;
	/* Freeing into the reserved pool only requires on-disk update... */
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_RES_FDBLOCKS, len);
	/* ...but freeing beyond that requires in-core and on-disk update. */
	if (len > leftover)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, len - leftover);
}
