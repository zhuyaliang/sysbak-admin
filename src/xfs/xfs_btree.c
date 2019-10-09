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
#include "xfs_shared.h"
#include "xfs_format.h"
#include <xfs/xfs_log_format.h>
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_btree.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_alloc.h"

/*
 * Cursor allocation zone.
 */
kmem_zone_t	*xfs_btree_cur_zone;

/*
 * Btree magic numbers.
 */
static const __uint32_t xfs_magics[2][XFS_BTNUM_MAX] = {
	{ XFS_ABTB_MAGIC, XFS_ABTC_MAGIC, 0, XFS_BMAP_MAGIC, XFS_IBT_MAGIC,
	  XFS_FIBT_MAGIC, 0 },
	{ XFS_ABTB_CRC_MAGIC, XFS_ABTC_CRC_MAGIC, XFS_RMAP_CRC_MAGIC,
	  XFS_BMAP_CRC_MAGIC, XFS_IBT_CRC_MAGIC, XFS_FIBT_CRC_MAGIC,
	  XFS_REFC_CRC_MAGIC }
};
#define xfs_btree_magic(cur) \
	xfs_magics[!!((cur)->bc_flags & XFS_BTREE_CRC_BLOCKS)][cur->bc_btnum]

STATIC int				/* error (0 or EFSCORRUPTED) */
xfs_btree_check_lblock(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	struct xfs_btree_block	*block,	/* btree long form block pointer */
	int			level,	/* level of the btree block */
	struct xfs_buf		*bp)	/* buffer for block, if any */
{
	int			lblock_ok = 1; /* block passes checks */
	struct xfs_mount	*mp;	/* file system mount point */

	mp = cur->bc_mp;

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		lblock_ok = lblock_ok &&
			uuid_equal(&block->bb_u.l.bb_uuid,
				   &mp->m_sb.sb_meta_uuid) &&
			block->bb_u.l.bb_blkno == cpu_to_be64(
				bp ? bp->b_bn : XFS_BUF_DADDR_NULL);
	}

	lblock_ok = lblock_ok &&
		be32_to_cpu(block->bb_magic) == xfs_btree_magic(cur) &&
		be16_to_cpu(block->bb_level) == level &&
		be16_to_cpu(block->bb_numrecs) <=
			cur->bc_ops->get_maxrecs(cur, level) &&
		block->bb_u.l.bb_leftsib &&
		(block->bb_u.l.bb_leftsib == cpu_to_be64(NULLFSBLOCK) ||
		 XFS_FSB_SANITY_CHECK(mp,
			be64_to_cpu(block->bb_u.l.bb_leftsib))) &&
		block->bb_u.l.bb_rightsib &&
		(block->bb_u.l.bb_rightsib == cpu_to_be64(NULLFSBLOCK) ||
		 XFS_FSB_SANITY_CHECK(mp,
			be64_to_cpu(block->bb_u.l.bb_rightsib)));

	if (unlikely(XFS_TEST_ERROR(!lblock_ok, mp,
			XFS_ERRTAG_BTREE_CHECK_LBLOCK,
			XFS_RANDOM_BTREE_CHECK_LBLOCK))) {
		if (bp)
			trace_xfs_btree_corrupt(bp, _RET_IP_);
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, mp);
		return -EFSCORRUPTED;
	}
	return 0;
}

STATIC int				/* error (0 or EFSCORRUPTED) */
xfs_btree_check_sblock(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	struct xfs_btree_block	*block,	/* btree short form block pointer */
	int			level,	/* level of the btree block */
	struct xfs_buf		*bp)	/* buffer containing block */
{
	struct xfs_mount	*mp;	/* file system mount point */
	struct xfs_buf		*agbp;	/* buffer for ag. freespace struct */
	struct xfs_agf		*agf;	/* ag. freespace structure */
	xfs_agblock_t		agflen;	/* native ag. freespace length */
	int			sblock_ok = 1; /* block passes checks */

	mp = cur->bc_mp;
	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	agflen = be32_to_cpu(agf->agf_length);

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		sblock_ok = sblock_ok &&
			uuid_equal(&block->bb_u.s.bb_uuid,
				   &mp->m_sb.sb_meta_uuid) &&
			block->bb_u.s.bb_blkno == cpu_to_be64(
				bp ? bp->b_bn : XFS_BUF_DADDR_NULL);
	}

	sblock_ok = sblock_ok &&
		be32_to_cpu(block->bb_magic) == xfs_btree_magic(cur) &&
		be16_to_cpu(block->bb_level) == level &&
		be16_to_cpu(block->bb_numrecs) <=
			cur->bc_ops->get_maxrecs(cur, level) &&
		(block->bb_u.s.bb_leftsib == cpu_to_be32(NULLAGBLOCK) ||
		 be32_to_cpu(block->bb_u.s.bb_leftsib) < agflen) &&
		block->bb_u.s.bb_leftsib &&
		(block->bb_u.s.bb_rightsib == cpu_to_be32(NULLAGBLOCK) ||
		 be32_to_cpu(block->bb_u.s.bb_rightsib) < agflen) &&
		block->bb_u.s.bb_rightsib;

	if (unlikely(XFS_TEST_ERROR(!sblock_ok, mp,
			XFS_ERRTAG_BTREE_CHECK_SBLOCK,
			XFS_RANDOM_BTREE_CHECK_SBLOCK))) {
		if (bp)
			trace_xfs_btree_corrupt(bp, _RET_IP_);
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, mp);
		return -EFSCORRUPTED;
	}
	return 0;
}

/*
 * Debug routine: check that block header is ok.
 */
int
xfs_btree_check_block(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	struct xfs_btree_block	*block,	/* generic btree block pointer */
	int			level,	/* level of the btree block */
	struct xfs_buf		*bp)	/* buffer containing block, if any */
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return xfs_btree_check_lblock(cur, block, level, bp);
	else
		return xfs_btree_check_sblock(cur, block, level, bp);
}

/*
 * Check that (long) pointer is ok.
 */
int					/* error (0 or EFSCORRUPTED) */
xfs_btree_check_lptr(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_fsblock_t		bno,	/* btree block disk address */
	int			level)	/* btree block level */
{
	XFS_WANT_CORRUPTED_RETURN(cur->bc_mp,
		level > 0 &&
		bno != NULLFSBLOCK &&
		XFS_FSB_SANITY_CHECK(cur->bc_mp, bno));
	return 0;
}

#ifdef DEBUG
/*
 * Check that (short) pointer is ok.
 */
STATIC int				/* error (0 or EFSCORRUPTED) */
xfs_btree_check_sptr(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* btree block disk address */
	int			level)	/* btree block level */
{
	xfs_agblock_t		agblocks = cur->bc_mp->m_sb.sb_agblocks;

	XFS_WANT_CORRUPTED_RETURN(cur->bc_mp,
		level > 0 &&
		bno != NULLAGBLOCK &&
		bno != 0 &&
		bno < agblocks);
	return 0;
}

/*
 * Check that block ptr is ok.
 */
STATIC int				/* error (0 or EFSCORRUPTED) */
xfs_btree_check_ptr(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	union xfs_btree_ptr	*ptr,	/* btree block disk address */
	int			index,	/* offset from ptr to check */
	int			level)	/* btree block level */
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		return xfs_btree_check_lptr(cur,
				be64_to_cpu((&ptr->l)[index]), level);
	} else {
		return xfs_btree_check_sptr(cur,
				be32_to_cpu((&ptr->s)[index]), level);
	}
}
#endif

/*
 * Calculate CRC on the whole btree block and stuff it into the
 * long-form btree header.
 *
 * Prior to calculting the CRC, pull the LSN out of the buffer log item and put
 * it into the buffer so recovery knows what the last modification was that made
 * it to disk.
 */
void
xfs_btree_lblock_calc_crc(
	struct xfs_buf		*bp)
{
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_buf_log_item	*bip = bp->b_fspriv;

	if (!xfs_sb_version_hascrc(&bp->b_target->bt_mount->m_sb))
		return;
	if (bip)
		block->bb_u.l.bb_lsn = cpu_to_be64(bip->bli_item.li_lsn);
	xfs_buf_update_cksum(bp, XFS_BTREE_LBLOCK_CRC_OFF);
}

bool
xfs_btree_lblock_verify_crc(
	struct xfs_buf		*bp)
{
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_mount	*mp = bp->b_target->bt_mount;

	if (xfs_sb_version_hascrc(&bp->b_target->bt_mount->m_sb)) {
		if (!xfs_log_check_lsn(mp, be64_to_cpu(block->bb_u.l.bb_lsn)))
			return false;
		return xfs_buf_verify_cksum(bp, XFS_BTREE_LBLOCK_CRC_OFF);
	}

	return true;
}

/*
 * Calculate CRC on the whole btree block and stuff it into the
 * short-form btree header.
 *
 * Prior to calculting the CRC, pull the LSN out of the buffer log item and put
 * it into the buffer so recovery knows what the last modification was that made
 * it to disk.
 */
void
xfs_btree_sblock_calc_crc(
	struct xfs_buf		*bp)
{
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_buf_log_item	*bip = bp->b_fspriv;

	if (!xfs_sb_version_hascrc(&bp->b_target->bt_mount->m_sb))
		return;
	if (bip)
		block->bb_u.s.bb_lsn = cpu_to_be64(bip->bli_item.li_lsn);
	xfs_buf_update_cksum(bp, XFS_BTREE_SBLOCK_CRC_OFF);
}

bool
xfs_btree_sblock_verify_crc(
	struct xfs_buf		*bp)
{
	struct xfs_btree_block  *block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_mount	*mp = bp->b_target->bt_mount;

	if (xfs_sb_version_hascrc(&bp->b_target->bt_mount->m_sb)) {
		if (!xfs_log_check_lsn(mp, be64_to_cpu(block->bb_u.s.bb_lsn)))
			return false;
		return xfs_buf_verify_cksum(bp, XFS_BTREE_SBLOCK_CRC_OFF);
	}

	return true;
}

static int
xfs_btree_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	int			error;

	error = cur->bc_ops->free_block(cur, bp);
	if (!error) {
		xfs_trans_binval(cur->bc_tp, bp);
		XFS_BTREE_STATS_INC(cur, free);
	}
	return error;
}

/*
 * Delete the btree cursor.
 */
void
xfs_btree_del_cursor(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		error)		/* del because of error */
{
	int		i;		/* btree level */

	/*
	 * Clear the buffer pointers, and release the buffers.
	 * If we're doing this in the face of an error, we
	 * need to make sure to inspect all of the entries
	 * in the bc_bufs array for buffers to be unlocked.
	 * This is because some of the btree code works from
	 * level n down to 0, and if we get an error along
	 * the way we won't have initialized all the entries
	 * down to 0.
	 */
	for (i = 0; i < cur->bc_nlevels; i++) {
		if (cur->bc_bufs[i])
			xfs_trans_brelse(cur->bc_tp, cur->bc_bufs[i]);
		else if (!error)
			break;
	}
	/*
	 * Can't free a bmap cursor without having dealt with the
	 * allocated indirect blocks' accounting.
	 */
	ASSERT(cur->bc_btnum != XFS_BTNUM_BMAP ||
	       cur->bc_private.b.allocated == 0);
	/*
	 * Free the cursor.
	 */
	kmem_zone_free(xfs_btree_cur_zone, cur);
}

/*
 * XFS btree block layout and addressing:
 *
 * There are two types of blocks in the btree: leaf and non-leaf blocks.
 *
 * The leaf record start with a header then followed by records containing
 * the values.  A non-leaf block also starts with the same header, and
 * then first contains lookup keys followed by an equal number of pointers
 * to the btree blocks at the previous level.
 *
 *		+--------+-------+-------+-------+-------+-------+-------+
 * Leaf:	| header | rec 1 | rec 2 | rec 3 | rec 4 | rec 5 | rec N |
 *		+--------+-------+-------+-------+-------+-------+-------+
 *
 *		+--------+-------+-------+-------+-------+-------+-------+
 * Non-Leaf:	| header | key 1 | key 2 | key N | ptr 1 | ptr 2 | ptr N |
 *		+--------+-------+-------+-------+-------+-------+-------+
 *
 * The header is called struct xfs_btree_block for reasons better left unknown
 * and comes in different versions for short (32bit) and long (64bit) block
 * pointers.  The record and key structures are defined by the btree instances
 * and opaque to the btree core.  The block pointers are simple disk endian
 * integers, available in a short (32bit) and long (64bit) variant.
 *
 * The helpers below calculate the offset of a given record, key or pointer
 * into a btree block (xfs_btree_*_offset) or return a pointer to the given
 * record, key or pointer (xfs_btree_*_addr).  Note that all addressing
 * inside the btree block is done using indices starting at one, not zero!
 *
 * If XFS_BTREE_OVERLAPPING is set, then this btree supports keys containing
 * overlapping intervals.  In such a tree, records are still sorted lowest to
 * highest and indexed by the smallest key value that refers to the record.
 * However, nodes are different: each pointer has two associated keys -- one
 * indexing the lowest key available in the block(s) below (the same behavior
 * as the key in a regular btree) and another indexing the highest key
 * available in the block(s) below.  Because records are /not/ sorted by the
 * highest key, all leaf block updates require us to compute the highest key
 * that matches any record in the leaf and to recursively update the high keys
 * in the nodes going further up in the tree, if necessary.  Nodes look like
 * this:
 *
 *		+--------+-----+-----+-----+-----+-----+-------+-------+-----+
 * Non-Leaf:	| header | lo1 | hi1 | lo2 | hi2 | ... | ptr 1 | ptr 2 | ... |
 *		+--------+-----+-----+-----+-----+-----+-------+-------+-----+
 *
 * To perform an interval query on an overlapped tree, perform the usual
 * depth-first search and use the low and high keys to decide if we can skip
 * that particular node.  If a leaf node is reached, return the records that
 * intersect the interval.  Note that an interval query may return numerous
 * entries.  For a non-overlapped tree, simply search for the record associated
 * with the lowest key and iterate forward until a non-matching record is
 * found.  Section 14.3 ("Interval Trees") of _Introduction to Algorithms_ by
 * Cormen, Leiserson, Rivest, and Stein (2nd or 3rd ed. only) discuss this in
 * more detail.
 *
 * Why do we care about overlapping intervals?  Let's say you have a bunch of
 * reverse mapping records on a reflink filesystem:
 *
 * 1: +- file A startblock B offset C length D -----------+
 * 2:      +- file E startblock F offset G length H --------------+
 * 3:      +- file I startblock F offset J length K --+
 * 4:                                                        +- file L... --+
 *
 * Now say we want to map block (B+D) into file A at offset (C+D).  Ideally,
 * we'd simply increment the length of record 1.  But how do we find the record
 * that ends at (B+D-1) (i.e. record 1)?  A LE lookup of (B+D-1) would return
 * record 3 because the keys are ordered first by startblock.  An interval
 * query would return records 1 and 2 because they both overlap (B+D-1), and
 * from that we can pick out record 1 as the appropriate left neighbor.
 *
 * In the non-overlapped case you can do a LE lookup and decrement the cursor
 * because a record's interval must end before the next record.
 */

/*
 * Return size of the btree block header for this btree instance.
 */
static inline size_t xfs_btree_block_len(struct xfs_btree_cur *cur)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (cur->bc_flags & XFS_BTREE_CRC_BLOCKS)
			return XFS_BTREE_LBLOCK_CRC_LEN;
		return XFS_BTREE_LBLOCK_LEN;
	}
	if (cur->bc_flags & XFS_BTREE_CRC_BLOCKS)
		return XFS_BTREE_SBLOCK_CRC_LEN;
	return XFS_BTREE_SBLOCK_LEN;
}

/*
 * Return size of btree block pointers for this btree instance.
 */
static inline size_t xfs_btree_ptr_len(struct xfs_btree_cur *cur)
{
	return (cur->bc_flags & XFS_BTREE_LONG_PTRS) ?
		sizeof(__be64) : sizeof(__be32);
}

/*
 * Calculate offset of the n-th record in a btree block.
 */
STATIC size_t
xfs_btree_rec_offset(
	struct xfs_btree_cur	*cur,
	int			n)
{
	return xfs_btree_block_len(cur) +
		(n - 1) * cur->bc_ops->rec_len;
}

/*
 * Calculate offset of the n-th key in a btree block.
 */
STATIC size_t
xfs_btree_key_offset(
	struct xfs_btree_cur	*cur,
	int			n)
{
	return xfs_btree_block_len(cur) +
		(n - 1) * cur->bc_ops->key_len;
}

/*
 * Calculate offset of the n-th high key in a btree block.
 */
STATIC size_t
xfs_btree_high_key_offset(
	struct xfs_btree_cur	*cur,
	int			n)
{
	return xfs_btree_block_len(cur) +
		(n - 1) * cur->bc_ops->key_len + (cur->bc_ops->key_len / 2);
}

/*
 * Calculate offset of the n-th block pointer in a btree block.
 */
STATIC size_t
xfs_btree_ptr_offset(
	struct xfs_btree_cur	*cur,
	int			n,
	int			level)
{
	return xfs_btree_block_len(cur) +
		cur->bc_ops->get_maxrecs(cur, level) * cur->bc_ops->key_len +
		(n - 1) * xfs_btree_ptr_len(cur);
}

/*
 * Return a pointer to the n-th record in the btree block.
 */
STATIC union xfs_btree_rec *
xfs_btree_rec_addr(
	struct xfs_btree_cur	*cur,
	int			n,
	struct xfs_btree_block	*block)
{
	return (union xfs_btree_rec *)
		((char *)block + xfs_btree_rec_offset(cur, n));
}

/*
 * Return a pointer to the n-th key in the btree block.
 */
STATIC union xfs_btree_key *
xfs_btree_key_addr(
	struct xfs_btree_cur	*cur,
	int			n,
	struct xfs_btree_block	*block)
{
	return (union xfs_btree_key *)
		((char *)block + xfs_btree_key_offset(cur, n));
}

/*
 * Return a pointer to the n-th high key in the btree block.
 */
STATIC union xfs_btree_key *
xfs_btree_high_key_addr(
	struct xfs_btree_cur	*cur,
	int			n,
	struct xfs_btree_block	*block)
{
	return (union xfs_btree_key *)
		((char *)block + xfs_btree_high_key_offset(cur, n));
}

/*
 * Return a pointer to the n-th block pointer in the btree block.
 */
STATIC union xfs_btree_ptr *
xfs_btree_ptr_addr(
	struct xfs_btree_cur	*cur,
	int			n,
	struct xfs_btree_block	*block)
{
	int			level = xfs_btree_get_level(block);

	ASSERT(block->bb_level != 0);

	return (union xfs_btree_ptr *)
		((char *)block + xfs_btree_ptr_offset(cur, n, level));
}

/*
 * Get the root block which is stored in the inode.
 *
 * For now this btree implementation assumes the btree root is always
 * stored in the if_broot field of an inode fork.
 */
STATIC struct xfs_btree_block *
xfs_btree_get_iroot(
	struct xfs_btree_cur	*cur)
{
	struct xfs_ifork	*ifp;

	ifp = XFS_IFORK_PTR(cur->bc_private.b.ip, cur->bc_private.b.whichfork);
	return (struct xfs_btree_block *)ifp->if_broot;
}

/*
 * Retrieve the block pointer from the cursor at the given level.
 * This may be an inode btree root or from a buffer.
 */
STATIC struct xfs_btree_block *		/* generic btree block pointer */
xfs_btree_get_block(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level,	/* level in btree */
	struct xfs_buf		**bpp)	/* buffer containing the block */
{
	if ((cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    (level == cur->bc_nlevels - 1)) {
		*bpp = NULL;
		return xfs_btree_get_iroot(cur);
	}

	*bpp = cur->bc_bufs[level];
	return XFS_BUF_TO_BLOCK(*bpp);
}

/*
 * Check for the cursor referring to the last block at the given level.
 */
int					/* 1=is last block, 0=not last block */
xfs_btree_islastblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to check */
{
	struct xfs_btree_block	*block;	/* generic btree block pointer */
	xfs_buf_t		*bp;	/* buffer containing block */

	block = xfs_btree_get_block(cur, level, &bp);
	xfs_btree_check_block(cur, block, level, bp);
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return block->bb_u.l.bb_rightsib == cpu_to_be64(NULLFSBLOCK);
	else
		return block->bb_u.s.bb_rightsib == cpu_to_be32(NULLAGBLOCK);
}

/*
 * Change the cursor to point to the first record at the given level.
 * Other levels are unaffected.
 */
STATIC int				/* success=1, failure=0 */
xfs_btree_firstrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to change */
{
	struct xfs_btree_block	*block;	/* generic btree block pointer */
	xfs_buf_t		*bp;	/* buffer containing block */

	/*
	 * Get the block pointer for this level.
	 */
	block = xfs_btree_get_block(cur, level, &bp);
	xfs_btree_check_block(cur, block, level, bp);
	/*
	 * It's empty, there is no such record.
	 */
	if (!block->bb_numrecs)
		return 0;
	/*
	 * Set the ptr value to 1, that's the first record/key.
	 */
	cur->bc_ptrs[level] = 1;
	return 1;
}

/*
 * Change the cursor to point to the last record in the current block
 * at the given level.  Other levels are unaffected.
 */
STATIC int				/* success=1, failure=0 */
xfs_btree_lastrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to change */
{
	struct xfs_btree_block	*block;	/* generic btree block pointer */
	xfs_buf_t		*bp;	/* buffer containing block */

	/*
	 * Get the block pointer for this level.
	 */
	block = xfs_btree_get_block(cur, level, &bp);
	xfs_btree_check_block(cur, block, level, bp);
	/*
	 * It's empty, there is no such record.
	 */
	if (!block->bb_numrecs)
		return 0;
	/*
	 * Set the ptr value to numrecs, that's the last record/key.
	 */
	cur->bc_ptrs[level] = be16_to_cpu(block->bb_numrecs);
	return 1;
}

/*
 * Compute first and last byte offsets for the fields given.
 * Interprets the offsets table, which contains struct field offsets.
 */
void
xfs_btree_offsets(
	__int64_t	fields,		/* bitmask of fields */
	const short	*offsets,	/* table of field offsets */
	int		nbits,		/* number of bits to inspect */
	int		*first,		/* output: first byte offset */
	int		*last)		/* output: last byte offset */
{
	int		i;		/* current bit number */
	__int64_t	imask;		/* mask for current bit number */

	ASSERT(fields != 0);
	/*
	 * Find the lowest bit, so the first byte offset.
	 */
	for (i = 0, imask = 1LL; ; i++, imask <<= 1) {
		if (imask & fields) {
			*first = offsets[i];
			break;
		}
	}
	/*
	 * Find the highest bit, so the last byte offset.
	 */
	for (i = nbits - 1, imask = 1LL << i; ; i--, imask >>= 1) {
		if (imask & fields) {
			*last = offsets[i + 1] - 1;
			break;
		}
	}
}

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Long-form addressing.
 */
/* ARGSUSED */
void
xfs_btree_reada_bufl(
	struct xfs_mount	*mp,		/* file system mount point */
	xfs_fsblock_t		fsbno,		/* file system block number */
	xfs_extlen_t		count,		/* count of filesystem blocks */
	const struct xfs_buf_ops *ops)
{
	xfs_daddr_t		d;

	ASSERT(fsbno != NULLFSBLOCK);
	d = XFS_FSB_TO_DADDR(mp, fsbno);
	xfs_buf_readahead(mp->m_ddev_targp, d, mp->m_bsize * count, ops);
}

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Short-form addressing.
 */
/* ARGSUSED */
void
xfs_btree_reada_bufs(
	struct xfs_mount	*mp,		/* file system mount point */
	xfs_agnumber_t		agno,		/* allocation group number */
	xfs_agblock_t		agbno,		/* allocation group block number */
	xfs_extlen_t		count,		/* count of filesystem blocks */
	const struct xfs_buf_ops *ops)
{
	xfs_daddr_t		d;

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agbno != NULLAGBLOCK);
	d = XFS_AGB_TO_DADDR(mp, agno, agbno);
	xfs_buf_readahead(mp->m_ddev_targp, d, mp->m_bsize * count, ops);
}

STATIC int
xfs_btree_readahead_lblock(
	struct xfs_btree_cur	*cur,
	int			lr,
	struct xfs_btree_block	*block)
{
	int			rval = 0;
	xfs_fsblock_t		left = be64_to_cpu(block->bb_u.l.bb_leftsib);
	xfs_fsblock_t		right = be64_to_cpu(block->bb_u.l.bb_rightsib);

	if ((lr & XFS_BTCUR_LEFTRA) && left != NULLFSBLOCK) {
		xfs_btree_reada_bufl(cur->bc_mp, left, 1,
				     cur->bc_ops->buf_ops);
		rval++;
	}

	if ((lr & XFS_BTCUR_RIGHTRA) && right != NULLFSBLOCK) {
		xfs_btree_reada_bufl(cur->bc_mp, right, 1,
				     cur->bc_ops->buf_ops);
		rval++;
	}

	return rval;
}

STATIC int
xfs_btree_readahead_sblock(
	struct xfs_btree_cur	*cur,
	int			lr,
	struct xfs_btree_block *block)
{
	int			rval = 0;
	xfs_agblock_t		left = be32_to_cpu(block->bb_u.s.bb_leftsib);
	xfs_agblock_t		right = be32_to_cpu(block->bb_u.s.bb_rightsib);


	if ((lr & XFS_BTCUR_LEFTRA) && left != NULLAGBLOCK) {
		xfs_btree_reada_bufs(cur->bc_mp, cur->bc_private.a.agno,
				     left, 1, cur->bc_ops->buf_ops);
		rval++;
	}

	if ((lr & XFS_BTCUR_RIGHTRA) && right != NULLAGBLOCK) {
		xfs_btree_reada_bufs(cur->bc_mp, cur->bc_private.a.agno,
				     right, 1, cur->bc_ops->buf_ops);
		rval++;
	}

	return rval;
}

/*
 * Read-ahead btree blocks, at the given level.
 * Bits in lr are set from XFS_BTCUR_{LEFT,RIGHT}RA.
 */
STATIC int
xfs_btree_readahead(
	struct xfs_btree_cur	*cur,		/* btree cursor */
	int			lev,		/* level in btree */
	int			lr)		/* left/right bits */
{
	struct xfs_btree_block	*block;

	/*
	 * No readahead needed if we are at the root level and the
	 * btree root is stored in the inode.
	 */
	if ((cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    (lev == cur->bc_nlevels - 1))
		return 0;

	if ((cur->bc_ra[lev] | lr) == cur->bc_ra[lev])
		return 0;

	cur->bc_ra[lev] |= lr;
	block = XFS_BUF_TO_BLOCK(cur->bc_bufs[lev]);

	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return xfs_btree_readahead_lblock(cur, lr, block);
	return xfs_btree_readahead_sblock(cur, lr, block);
}

STATIC xfs_daddr_t
xfs_btree_ptr_to_daddr(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		ASSERT(ptr->l != cpu_to_be64(NULLFSBLOCK));

		return XFS_FSB_TO_DADDR(cur->bc_mp, be64_to_cpu(ptr->l));
	} else {
		ASSERT(cur->bc_private.a.agno != NULLAGNUMBER);
		ASSERT(ptr->s != cpu_to_be32(NULLAGBLOCK));

		return XFS_AGB_TO_DADDR(cur->bc_mp, cur->bc_private.a.agno,
					be32_to_cpu(ptr->s));
	}
}

/*
 * Readahead @count btree blocks at the given @ptr location.
 *
 * We don't need to care about long or short form btrees here as we have a
 * method of converting the ptr directly to a daddr available to us.
 */
STATIC void
xfs_btree_readahead_ptr(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	xfs_extlen_t		count)
{
	xfs_buf_readahead(cur->bc_mp->m_ddev_targp,
			  xfs_btree_ptr_to_daddr(cur, ptr),
			  cur->bc_mp->m_bsize * count, cur->bc_ops->buf_ops);
}

/*
 * Set the buffer for level "lev" in the cursor to bp, releasing
 * any previous buffer.
 */
STATIC void
xfs_btree_setbuf(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			lev,	/* level in btree */
	xfs_buf_t		*bp)	/* new buffer to set */
{
	struct xfs_btree_block	*b;	/* btree block */

	if (cur->bc_bufs[lev])
		xfs_trans_brelse(cur->bc_tp, cur->bc_bufs[lev]);
	cur->bc_bufs[lev] = bp;
	cur->bc_ra[lev] = 0;

	b = XFS_BUF_TO_BLOCK(bp);
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (b->bb_u.l.bb_leftsib == cpu_to_be64(NULLFSBLOCK))
			cur->bc_ra[lev] |= XFS_BTCUR_LEFTRA;
		if (b->bb_u.l.bb_rightsib == cpu_to_be64(NULLFSBLOCK))
			cur->bc_ra[lev] |= XFS_BTCUR_RIGHTRA;
	} else {
		if (b->bb_u.s.bb_leftsib == cpu_to_be32(NULLAGBLOCK))
			cur->bc_ra[lev] |= XFS_BTCUR_LEFTRA;
		if (b->bb_u.s.bb_rightsib == cpu_to_be32(NULLAGBLOCK))
			cur->bc_ra[lev] |= XFS_BTCUR_RIGHTRA;
	}
}

STATIC int
xfs_btree_ptr_is_null(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return ptr->l == cpu_to_be64(NULLFSBLOCK);
	else
		return ptr->s == cpu_to_be32(NULLAGBLOCK);
}

STATIC void
xfs_btree_set_ptr_null(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		ptr->l = cpu_to_be64(NULLFSBLOCK);
	else
		ptr->s = cpu_to_be32(NULLAGBLOCK);
}

/*
 * Get/set/init sibling pointers
 */
STATIC void
xfs_btree_get_sibling(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_ptr	*ptr,
	int			lr)
{
	ASSERT(lr == XFS_BB_LEFTSIB || lr == XFS_BB_RIGHTSIB);

	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (lr == XFS_BB_RIGHTSIB)
			ptr->l = block->bb_u.l.bb_rightsib;
		else
			ptr->l = block->bb_u.l.bb_leftsib;
	} else {
		if (lr == XFS_BB_RIGHTSIB)
			ptr->s = block->bb_u.s.bb_rightsib;
		else
			ptr->s = block->bb_u.s.bb_leftsib;
	}
}

STATIC void
xfs_btree_set_sibling(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_ptr	*ptr,
	int			lr)
{
	ASSERT(lr == XFS_BB_LEFTSIB || lr == XFS_BB_RIGHTSIB);

	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (lr == XFS_BB_RIGHTSIB)
			block->bb_u.l.bb_rightsib = ptr->l;
		else
			block->bb_u.l.bb_leftsib = ptr->l;
	} else {
		if (lr == XFS_BB_RIGHTSIB)
			block->bb_u.s.bb_rightsib = ptr->s;
		else
			block->bb_u.s.bb_leftsib = ptr->s;
	}
}

void
xfs_btree_init_block_int(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*buf,
	xfs_daddr_t		blkno,
	__u32			magic,
	__u16			level,
	__u16			numrecs,
	__u64			owner,
	unsigned int		flags)
{
	buf->bb_magic = cpu_to_be32(magic);
	buf->bb_level = cpu_to_be16(level);
	buf->bb_numrecs = cpu_to_be16(numrecs);

	if (flags & XFS_BTREE_LONG_PTRS) {
		buf->bb_u.l.bb_leftsib = cpu_to_be64(NULLFSBLOCK);
		buf->bb_u.l.bb_rightsib = cpu_to_be64(NULLFSBLOCK);
		if (flags & XFS_BTREE_CRC_BLOCKS) {
			buf->bb_u.l.bb_blkno = cpu_to_be64(blkno);
			buf->bb_u.l.bb_owner = cpu_to_be64(owner);
			uuid_copy(&buf->bb_u.l.bb_uuid, &mp->m_sb.sb_meta_uuid);
			buf->bb_u.l.bb_pad = 0;
			buf->bb_u.l.bb_lsn = 0;
		}
	} else {
		/* owner is a 32 bit value on short blocks */
		__u32 __owner = (__u32)owner;

		buf->bb_u.s.bb_leftsib = cpu_to_be32(NULLAGBLOCK);
		buf->bb_u.s.bb_rightsib = cpu_to_be32(NULLAGBLOCK);
		if (flags & XFS_BTREE_CRC_BLOCKS) {
			buf->bb_u.s.bb_blkno = cpu_to_be64(blkno);
			buf->bb_u.s.bb_owner = cpu_to_be32(__owner);
			uuid_copy(&buf->bb_u.s.bb_uuid, &mp->m_sb.sb_meta_uuid);
			buf->bb_u.s.bb_lsn = 0;
		}
	}
}

void
xfs_btree_init_block(
	struct xfs_mount *mp,
	struct xfs_buf	*bp,
	__u32		magic,
	__u16		level,
	__u16		numrecs,
	__u64		owner,
	unsigned int	flags)
{
	xfs_btree_init_block_int(mp, XFS_BUF_TO_BLOCK(bp), bp->b_bn,
				 magic, level, numrecs, owner, flags);
}

STATIC void
xfs_btree_init_block_cur(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	int			level,
	int			numrecs)
{
	__u64 owner;

	/*
	 * we can pull the owner from the cursor right now as the different
	 * owners align directly with the pointer size of the btree. This may
	 * change in future, but is safe for current users of the generic btree
	 * code.
	 */
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		owner = cur->bc_private.b.ip->i_ino;
	else
		owner = cur->bc_private.a.agno;

	xfs_btree_init_block_int(cur->bc_mp, XFS_BUF_TO_BLOCK(bp), bp->b_bn,
				 xfs_btree_magic(cur), level, numrecs,
				 owner, cur->bc_flags);
}

/*
 * Return true if ptr is the last record in the btree and
 * we need to track updates to this record.  The decision
 * will be further refined in the update_lastrec method.
 */
STATIC int
xfs_btree_is_lastrec(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	int			level)
{
	union xfs_btree_ptr	ptr;

	if (level > 0)
		return 0;
	if (!(cur->bc_flags & XFS_BTREE_LASTREC_UPDATE))
		return 0;

	xfs_btree_get_sibling(cur, block, &ptr, XFS_BB_RIGHTSIB);
	if (!xfs_btree_ptr_is_null(cur, &ptr))
		return 0;
	return 1;
}

STATIC void
xfs_btree_buf_to_ptr(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	union xfs_btree_ptr	*ptr)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		ptr->l = cpu_to_be64(XFS_DADDR_TO_FSB(cur->bc_mp,
					XFS_BUF_ADDR(bp)));
	else {
		ptr->s = cpu_to_be32(xfs_daddr_to_agbno(cur->bc_mp,
					XFS_BUF_ADDR(bp)));
	}
}

STATIC void
xfs_btree_set_refs(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	switch (cur->bc_btnum) {
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		xfs_buf_set_ref(bp, XFS_ALLOC_BTREE_REF);
		break;
	case XFS_BTNUM_INO:
	case XFS_BTNUM_FINO:
		xfs_buf_set_ref(bp, XFS_INO_BTREE_REF);
		break;
	case XFS_BTNUM_BMAP:
		xfs_buf_set_ref(bp, XFS_BMAP_BTREE_REF);
		break;
	case XFS_BTNUM_RMAP:
		xfs_buf_set_ref(bp, XFS_RMAP_BTREE_REF);
		break;
	case XFS_BTNUM_REFC:
		xfs_buf_set_ref(bp, XFS_REFC_BTREE_REF);
		break;
	default:
		ASSERT(0);
	}
}

STATIC int
xfs_btree_get_buf_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	int			flags,
	struct xfs_btree_block	**block,
	struct xfs_buf		**bpp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	xfs_daddr_t		d;

	/* need to sort out how callers deal with failures first */
	ASSERT(!(flags & XBF_TRYLOCK));

	d = xfs_btree_ptr_to_daddr(cur, ptr);
	*bpp = xfs_trans_get_buf(cur->bc_tp, mp->m_ddev_targp, d,
				 mp->m_bsize, flags);

	if (!*bpp)
		return -ENOMEM;

	(*bpp)->b_ops = cur->bc_ops->buf_ops;
	*block = XFS_BUF_TO_BLOCK(*bpp);
	return 0;
}

/*
 * Copy keys from one btree block to another.
 */
STATIC void
xfs_btree_copy_keys(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*dst_key,
	union xfs_btree_key	*src_key,
	int			numkeys)
{
	ASSERT(numkeys >= 0);
	memcpy(dst_key, src_key, numkeys * cur->bc_ops->key_len);
}

/*
 * Copy records from one btree block to another.
 */
STATIC void
xfs_btree_copy_recs(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*dst_rec,
	union xfs_btree_rec	*src_rec,
	int			numrecs)
{
	ASSERT(numrecs >= 0);
	memcpy(dst_rec, src_rec, numrecs * cur->bc_ops->rec_len);
}

/*
 * Copy block pointers from one btree block to another.
 */
STATIC void
xfs_btree_copy_ptrs(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*dst_ptr,
	union xfs_btree_ptr	*src_ptr,
	int			numptrs)
{
	ASSERT(numptrs >= 0);
	memcpy(dst_ptr, src_ptr, numptrs * xfs_btree_ptr_len(cur));
}

/*
 * Shift keys one index left/right inside a single btree block.
 */
STATIC void
xfs_btree_shift_keys(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key,
	int			dir,
	int			numkeys)
{
	char			*dst_key;

	ASSERT(numkeys >= 0);
	ASSERT(dir == 1 || dir == -1);

	dst_key = (char *)key + (dir * cur->bc_ops->key_len);
	memmove(dst_key, key, numkeys * cur->bc_ops->key_len);
}

/*
 * Shift records one index left/right inside a single btree block.
 */
STATIC void
xfs_btree_shift_recs(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec,
	int			dir,
	int			numrecs)
{
	char			*dst_rec;

	ASSERT(numrecs >= 0);
	ASSERT(dir == 1 || dir == -1);

	dst_rec = (char *)rec + (dir * cur->bc_ops->rec_len);
	memmove(dst_rec, rec, numrecs * cur->bc_ops->rec_len);
}

/*
 * Shift block pointers one index left/right inside a single btree block.
 */
STATIC void
xfs_btree_shift_ptrs(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	int			dir,
	int			numptrs)
{
	char			*dst_ptr;

	ASSERT(numptrs >= 0);
	ASSERT(dir == 1 || dir == -1);

	dst_ptr = (char *)ptr + (dir * xfs_btree_ptr_len(cur));
	memmove(dst_ptr, ptr, numptrs * xfs_btree_ptr_len(cur));
}

/*
 * Log key values from the btree block.
 */
STATIC void
xfs_btree_log_keys(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	int			first,
	int			last)
{
	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);
	XFS_BTREE_TRACE_ARGBII(cur, bp, first, last);

	if (bp) {
		xfs_trans_buf_set_type(cur->bc_tp, bp, XFS_BLFT_BTREE_BUF);
		xfs_trans_log_buf(cur->bc_tp, bp,
				  xfs_btree_key_offset(cur, first),
				  xfs_btree_key_offset(cur, last + 1) - 1);
	} else {
		xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip,
				xfs_ilog_fbroot(cur->bc_private.b.whichfork));
	}

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
}

/*
 * Log record values from the btree block.
 */
void
xfs_btree_log_recs(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	int			first,
	int			last)
{
	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);
	XFS_BTREE_TRACE_ARGBII(cur, bp, first, last);

	xfs_trans_buf_set_type(cur->bc_tp, bp, XFS_BLFT_BTREE_BUF);
	xfs_trans_log_buf(cur->bc_tp, bp,
			  xfs_btree_rec_offset(cur, first),
			  xfs_btree_rec_offset(cur, last + 1) - 1);

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
}

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_btree_log_ptrs(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	struct xfs_buf		*bp,	/* buffer containing btree block */
	int			first,	/* index of first pointer to log */
	int			last)	/* index of last pointer to log */
{
	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);
	XFS_BTREE_TRACE_ARGBII(cur, bp, first, last);

	if (bp) {
		struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
		int			level = xfs_btree_get_level(block);

		xfs_trans_buf_set_type(cur->bc_tp, bp, XFS_BLFT_BTREE_BUF);
		xfs_trans_log_buf(cur->bc_tp, bp,
				xfs_btree_ptr_offset(cur, first, level),
				xfs_btree_ptr_offset(cur, last + 1, level) - 1);
	} else {
		xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip,
			xfs_ilog_fbroot(cur->bc_private.b.whichfork));
	}

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
}

/*
 * Log fields from a btree block header.
 */
void
xfs_btree_log_block(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	struct xfs_buf		*bp,	/* buffer containing btree block */
	int			fields)	/* mask of fields: XFS_BB_... */
{
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	static const short	soffsets[] = {	/* table of offsets (short) */
		offsetof(struct xfs_btree_block, bb_magic),
		offsetof(struct xfs_btree_block, bb_level),
		offsetof(struct xfs_btree_block, bb_numrecs),
		offsetof(struct xfs_btree_block, bb_u.s.bb_leftsib),
		offsetof(struct xfs_btree_block, bb_u.s.bb_rightsib),
		offsetof(struct xfs_btree_block, bb_u.s.bb_blkno),
		offsetof(struct xfs_btree_block, bb_u.s.bb_lsn),
		offsetof(struct xfs_btree_block, bb_u.s.bb_uuid),
		offsetof(struct xfs_btree_block, bb_u.s.bb_owner),
		offsetof(struct xfs_btree_block, bb_u.s.bb_crc),
		XFS_BTREE_SBLOCK_CRC_LEN
	};
	static const short	loffsets[] = {	/* table of offsets (long) */
		offsetof(struct xfs_btree_block, bb_magic),
		offsetof(struct xfs_btree_block, bb_level),
		offsetof(struct xfs_btree_block, bb_numrecs),
		offsetof(struct xfs_btree_block, bb_u.l.bb_leftsib),
		offsetof(struct xfs_btree_block, bb_u.l.bb_rightsib),
		offsetof(struct xfs_btree_block, bb_u.l.bb_blkno),
		offsetof(struct xfs_btree_block, bb_u.l.bb_lsn),
		offsetof(struct xfs_btree_block, bb_u.l.bb_uuid),
		offsetof(struct xfs_btree_block, bb_u.l.bb_owner),
		offsetof(struct xfs_btree_block, bb_u.l.bb_crc),
		offsetof(struct xfs_btree_block, bb_u.l.bb_pad),
		XFS_BTREE_LBLOCK_CRC_LEN
	};

	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);
	XFS_BTREE_TRACE_ARGBI(cur, bp, fields);

	if (bp) {
		int nbits;

		if (cur->bc_flags & XFS_BTREE_CRC_BLOCKS) {
			/*
			 * We don't log the CRC when updating a btree
			 * block but instead recreate it during log
			 * recovery.  As the log buffers have checksums
			 * of their own this is safe and avoids logging a crc
			 * update in a lot of places.
			 */
			if (fields == XFS_BB_ALL_BITS)
				fields = XFS_BB_ALL_BITS_CRC;
			nbits = XFS_BB_NUM_BITS_CRC;
		} else {
			nbits = XFS_BB_NUM_BITS;
		}
		xfs_btree_offsets(fields,
				  (cur->bc_flags & XFS_BTREE_LONG_PTRS) ?
					loffsets : soffsets,
				  nbits, &first, &last);
		xfs_trans_buf_set_type(cur->bc_tp, bp, XFS_BLFT_BTREE_BUF);
		xfs_trans_log_buf(cur->bc_tp, bp, first, last);
	} else {
		xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip,
			xfs_ilog_fbroot(cur->bc_private.b.whichfork));
	}

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
}

/*
 * Get current search key.  For level 0 we don't actually have a key
 * structure so we make one up from the record.  For all other levels
 * we just return the right key.
 */
STATIC union xfs_btree_key *
xfs_lookup_get_search_key(
	struct xfs_btree_cur	*cur,
	int			level,
	int			keyno,
	struct xfs_btree_block	*block,
	union xfs_btree_key	*kp)
{
	if (level == 0) {
		cur->bc_ops->init_key_from_rec(kp,
				xfs_btree_rec_addr(cur, keyno, block));
		return kp;
	}

	return xfs_btree_key_addr(cur, keyno, block);
}

/* Find the high key storage area from a regular key. */
STATIC union xfs_btree_key *
xfs_btree_high_key_from_key(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	ASSERT(cur->bc_flags & XFS_BTREE_OVERLAPPING);
	return (union xfs_btree_key *)((char *)key +
			(cur->bc_ops->key_len / 2));
}

/* Determine the low (and high if overlapped) keys of a leaf block */
STATIC void
xfs_btree_get_leaf_keys(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_key	*key)
{
	union xfs_btree_key	max_hkey;
	union xfs_btree_key	hkey;
	union xfs_btree_rec	*rec;
	union xfs_btree_key	*high;
	int			n;

	rec = xfs_btree_rec_addr(cur, 1, block);
	cur->bc_ops->init_key_from_rec(key, rec);

	if (cur->bc_flags & XFS_BTREE_OVERLAPPING) {

		cur->bc_ops->init_high_key_from_rec(&max_hkey, rec);
		for (n = 2; n <= xfs_btree_get_numrecs(block); n++) {
			rec = xfs_btree_rec_addr(cur, n, block);
			cur->bc_ops->init_high_key_from_rec(&hkey, rec);
			if (cur->bc_ops->diff_two_keys(cur, &hkey, &max_hkey)
					> 0)
				max_hkey = hkey;
		}

		high = xfs_btree_high_key_from_key(cur, key);
		memcpy(high, &max_hkey, cur->bc_ops->key_len / 2);
	}
}

/* Determine the low (and high if overlapped) keys of a node block */
STATIC void
xfs_btree_get_node_keys(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_key	*key)
{
	union xfs_btree_key	*hkey;
	union xfs_btree_key	*max_hkey;
	union xfs_btree_key	*high;
	int			n;

	if (cur->bc_flags & XFS_BTREE_OVERLAPPING) {
		memcpy(key, xfs_btree_key_addr(cur, 1, block),
				cur->bc_ops->key_len / 2);

		max_hkey = xfs_btree_high_key_addr(cur, 1, block);
		for (n = 2; n <= xfs_btree_get_numrecs(block); n++) {
			hkey = xfs_btree_high_key_addr(cur, n, block);
			if (cur->bc_ops->diff_two_keys(cur, hkey, max_hkey) > 0)
				max_hkey = hkey;
		}

		high = xfs_btree_high_key_from_key(cur, key);
		memcpy(high, max_hkey, cur->bc_ops->key_len / 2);
	} else {
		memcpy(key, xfs_btree_key_addr(cur, 1, block),
				cur->bc_ops->key_len);
	}
}

/* Derive the keys for any btree block. */
STATIC void
xfs_btree_get_keys(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_key	*key)
{
	if (be16_to_cpu(block->bb_level) == 0)
		xfs_btree_get_leaf_keys(cur, block, key);
	else
		xfs_btree_get_node_keys(cur, block, key);
}

/*
 * Decide if we need to update the parent keys of a btree block.  For
 * a standard btree this is only necessary if we're updating the first
 * record/key.  For an overlapping btree, we must always update the
 * keys because the highest key can be in any of the records or keys
 * in the block.
 */
static inline bool
xfs_btree_needs_key_update(
	struct xfs_btree_cur	*cur,
	int			ptr)
{
	return (cur->bc_flags & XFS_BTREE_OVERLAPPING) || ptr == 1;
}

/*
 * Update the low and high parent keys of the given level, progressing
 * towards the root.  If force_all is false, stop if the keys for a given
 * level do not need updating.
 */
STATIC int
__xfs_btree_updkeys(
	struct xfs_btree_cur	*cur,
	int			level,
	struct xfs_btree_block	*block,
	struct xfs_buf		*bp0,
	bool			force_all)
{
	union xfs_btree_key	key;	/* keys from current level */
	union xfs_btree_key	*lkey;	/* keys from the next level up */
	union xfs_btree_key	*hkey;
	union xfs_btree_key	*nlkey;	/* keys from the next level up */
	union xfs_btree_key	*nhkey;
	struct xfs_buf		*bp;
	int			ptr;

	ASSERT(cur->bc_flags & XFS_BTREE_OVERLAPPING);

	/* Exit if there aren't any parent levels to update. */
	if (level + 1 >= cur->bc_nlevels)
		return 0;

	trace_xfs_btree_updkeys(cur, level, bp0);

	lkey = &key;
	hkey = xfs_btree_high_key_from_key(cur, lkey);
	xfs_btree_get_keys(cur, block, lkey);
	for (level++; level < cur->bc_nlevels; level++) {
#ifdef DEBUG
		int		error;
#endif
		block = xfs_btree_get_block(cur, level, &bp);
		trace_xfs_btree_updkeys(cur, level, bp);
#ifdef DEBUG
		error = xfs_btree_check_block(cur, block, level, bp);
		if (error) {
			XFS_BTREE_TRACE_CURSOR(cur, XBT_ERROR);
			return error;
		}
#endif
		ptr = cur->bc_ptrs[level];
		nlkey = xfs_btree_key_addr(cur, ptr, block);
		nhkey = xfs_btree_high_key_addr(cur, ptr, block);
		if (!force_all &&
		    !(cur->bc_ops->diff_two_keys(cur, nlkey, lkey) != 0 ||
		      cur->bc_ops->diff_two_keys(cur, nhkey, hkey) != 0))
			break;
		xfs_btree_copy_keys(cur, nlkey, lkey, 1);
		xfs_btree_log_keys(cur, bp, ptr, ptr);
		if (level + 1 >= cur->bc_nlevels)
			break;
		xfs_btree_get_node_keys(cur, block, lkey);
	}

	return 0;
}

/* Update all the keys from some level in cursor back to the root. */
STATIC int
xfs_btree_updkeys_force(
	struct xfs_btree_cur	*cur,
	int			level)
{
	struct xfs_buf		*bp;
	struct xfs_btree_block	*block;

	block = xfs_btree_get_block(cur, level, &bp);
	return __xfs_btree_updkeys(cur, level, block, bp, true);
}

/*
 * Update the parent keys of the given level, progressing towards the root.
 */
STATIC int
xfs_btree_update_keys(
	struct xfs_btree_cur	*cur,
	int			level)
{
	struct xfs_btree_block	*block;
	struct xfs_buf		*bp;
	union xfs_btree_key	*kp;
	union xfs_btree_key	key;
	int			ptr;

	ASSERT(level >= 0);

	block = xfs_btree_get_block(cur, level, &bp);
	if (cur->bc_flags & XFS_BTREE_OVERLAPPING)
		return __xfs_btree_updkeys(cur, level, block, bp, false);

	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);
	XFS_BTREE_TRACE_ARGIK(cur, level, keyp);

	/*
	 * Go up the tree from this level toward the root.
	 * At each level, update the key value to the value input.
	 * Stop when we reach a level where the cursor isn't pointing
	 * at the first entry in the block.
	 */
	xfs_btree_get_keys(cur, block, &key);
	for (level++, ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
#ifdef DEBUG
		int		error;
#endif
		block = xfs_btree_get_block(cur, level, &bp);
#ifdef DEBUG
		error = xfs_btree_check_block(cur, block, level, bp);
		if (error) {
			XFS_BTREE_TRACE_CURSOR(cur, XBT_ERROR);
			return error;
		}
#endif
		ptr = cur->bc_ptrs[level];
		kp = xfs_btree_key_addr(cur, ptr, block);
		xfs_btree_copy_keys(cur, kp, &key, 1);
		xfs_btree_log_keys(cur, bp, ptr, ptr);
	}

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
	return 0;
}

/*
 * Update the record referred to by cur to the value in the
 * given record. This either works (return 0) or gets an
 * EFSCORRUPTED error.
 */
int
xfs_btree_update(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	struct xfs_btree_block	*block;
	struct xfs_buf		*bp;
	int			error;
	int			ptr;
	union xfs_btree_rec	*rp;

	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);
	XFS_BTREE_TRACE_ARGR(cur, rec);

	/* Pick up the current block. */
	block = xfs_btree_get_block(cur, 0, &bp);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, 0, bp);
	if (error)
		goto error0;
#endif
	/* Get the address of the rec to be updated. */
	ptr = cur->bc_ptrs[0];
	rp = xfs_btree_rec_addr(cur, ptr, block);

	/* Fill in the new contents and log them. */
	xfs_btree_copy_recs(cur, rp, rec, 1);
	xfs_btree_log_recs(cur, bp, ptr, ptr);

	/*
	 * If we are tracking the last record in the tree and
	 * we are at the far right edge of the tree, update it.
	 */
	if (xfs_btree_is_lastrec(cur, block, 0)) {
		cur->bc_ops->update_lastrec(cur, block, rec,
					    ptr, LASTREC_UPDATE);
	}

	/* Pass new key value up to our parent. */
	if (xfs_btree_needs_key_update(cur, ptr)) {
		error = xfs_btree_update_keys(cur, 0);
		if (error)
			goto error0;
	}

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
	return 0;

error0:
	XFS_BTREE_TRACE_CURSOR(cur, XBT_ERROR);
	return error;
}

#ifdef __KERNEL__
struct xfs_btree_split_args {
	struct xfs_btree_cur	*cur;
	int			level;
	union xfs_btree_ptr	*ptrp;
	union xfs_btree_key	*key;
	struct xfs_btree_cur	**curp;
	int			*stat;		/* success/failure */
	int			result;
	bool			kswapd;	/* allocation in kswapd context */
	struct completion	*done;
	struct work_struct	work;
};

#endif

/*
 * Change the owner of a btree.
 *
 * The mechanism we use here is ordered buffer logging. Because we don't know
 * how many buffers were are going to need to modify, we don't really want to
 * have to make transaction reservations for the worst case of every buffer in a
 * full size btree as that may be more space that we can fit in the log....
 *
 * We do the btree walk in the most optimal manner possible - we have sibling
 * pointers so we can just walk all the blocks on each level from left to right
 * in a single pass, and then move to the next level and do the same. We can
 * also do readahead on the sibling pointers to get IO moving more quickly,
 * though for slow disks this is unlikely to make much difference to performance
 * as the amount of CPU work we have to do before moving to the next block is
 * relatively small.
 *
 * For each btree block that we load, modify the owner appropriately, set the
 * buffer as an ordered buffer and log it appropriately. We need to ensure that
 * we mark the region we change dirty so that if the buffer is relogged in
 * a subsequent transaction the changes we make here as an ordered buffer are
 * correctly relogged in that transaction.  If we are in recovery context, then
 * just queue the modified buffer as delayed write buffer so the transaction
 * recovery completion writes the changes to disk.
 */
struct xfs_btree_block_change_owner_info {
	__uint64_t		new_owner;
	struct list_head	*buffer_list;
};

static int
xfs_btree_block_change_owner(
	struct xfs_btree_cur	*cur,
	int			level,
	void			*data)
{
	struct xfs_btree_block_change_owner_info	*bbcoi = data;
	struct xfs_btree_block	*block;
	struct xfs_buf		*bp;

	/* modify the owner */
	block = xfs_btree_get_block(cur, level, &bp);
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		block->bb_u.l.bb_owner = cpu_to_be64(bbcoi->new_owner);
	else
		block->bb_u.s.bb_owner = cpu_to_be32(bbcoi->new_owner);

	/*
	 * If the block is a root block hosted in an inode, we might not have a
	 * buffer pointer here and we shouldn't attempt to log the change as the
	 * information is already held in the inode and discarded when the root
	 * block is formatted into the on-disk inode fork. We still change it,
	 * though, so everything is consistent in memory.
	 */
	if (bp) {
		if (cur->bc_tp) {
			xfs_trans_ordered_buf(cur->bc_tp, bp);
			xfs_btree_log_block(cur, bp, XFS_BB_OWNER);
		} else {
			xfs_buf_delwri_queue(bp, bbcoi->buffer_list);
		}
	} else {
		ASSERT(cur->bc_flags & XFS_BTREE_ROOT_IN_INODE);
		ASSERT(level == cur->bc_nlevels - 1);
	}

	return 0;
}

/**
 * xfs_btree_sblock_v5hdr_verify() -- verify the v5 fields of a short-format
 *				      btree block
 *
 * @bp: buffer containing the btree block
 * @max_recs: pointer to the m_*_mxr max records field in the xfs mount
 * @pag_max_level: pointer to the per-ag max level field
 */
bool
xfs_btree_sblock_v5hdr_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_perag	*pag = bp->b_pag;

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return false;
	if (!uuid_equal(&block->bb_u.s.bb_uuid, &mp->m_sb.sb_meta_uuid))
		return false;
	if (block->bb_u.s.bb_blkno != cpu_to_be64(bp->b_bn))
		return false;
	if (pag && be32_to_cpu(block->bb_u.s.bb_owner) != pag->pag_agno)
		return false;
	return true;
}

/**
 * xfs_btree_sblock_verify() -- verify a short-format btree block
 *
 * @bp: buffer containing the btree block
 * @max_recs: maximum records allowed in this btree node
 */
bool
xfs_btree_sblock_verify(
	struct xfs_buf		*bp,
	unsigned int		max_recs)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);

	/* numrecs verification */
	if (be16_to_cpu(block->bb_numrecs) > max_recs)
		return false;

	/* sibling pointer verification */
	if (!block->bb_u.s.bb_leftsib ||
	    (be32_to_cpu(block->bb_u.s.bb_leftsib) >= mp->m_sb.sb_agblocks &&
	     block->bb_u.s.bb_leftsib != cpu_to_be32(NULLAGBLOCK)))
		return false;
	if (!block->bb_u.s.bb_rightsib ||
	    (be32_to_cpu(block->bb_u.s.bb_rightsib) >= mp->m_sb.sb_agblocks &&
	     block->bb_u.s.bb_rightsib != cpu_to_be32(NULLAGBLOCK)))
		return false;

	return true;
}

/*
 * Calculate the number of btree levels needed to store a given number of
 * records in a short-format btree.
 */
uint
xfs_btree_compute_maxlevels(
	struct xfs_mount	*mp,
	uint			*limits,
	unsigned long		len)
{
	uint			level;
	unsigned long		maxblocks;

	maxblocks = (len + limits[0] - 1) / limits[0];
	for (level = 1; maxblocks > 1; level++)
		maxblocks = (maxblocks + limits[1] - 1) / limits[1];
	return level;
}

/*
 * Calculate the number of blocks needed to store a given number of records
 * in a short-format (per-AG metadata) btree.
 */
xfs_extlen_t
xfs_btree_calc_size(
	struct xfs_mount	*mp,
	uint			*limits,
	unsigned long long	len)
{
	int			level;
	int			maxrecs;
	xfs_extlen_t		rval;

	maxrecs = limits[0];
	for (level = 0, rval = 0; len > 1; level++) {
		len += maxrecs - 1;
		do_div(len, maxrecs);
		maxrecs = limits[1];
		rval += len;
	}
	return rval;
}
