/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#ifndef __XFS_BMAP_H__
#define	__XFS_BMAP_H__
#include <xfs/xfs_format.h>

#define XFS_AGFL_SIZE(mp) \
    (((mp)->m_sb.sb_sectsize - \
     (xfs_sb_version_hascrc(&((mp)->m_sb)) ? \
        sizeof(struct xfs_agfl) : 0)) / \
      sizeof(xfs_agblock_t))
typedef enum {
    XFS_EXTFMT_NOSTATE = 0,
    XFS_EXTFMT_HASSTATE
} xfs_exntfmt_t;

typedef struct xfs_bmbt_irec1
{
    xfs_fileoff_t   br_startoff;    /* starting file offset */
    xfs_fsblock_t   br_startblock;  /* starting block number */
    xfs_filblks_t   br_blockcount;  /* number of blocks */
    xfs_exntfmt_t   br_state;   /* extent state */
} xfs_bmbt_irec_t1;

struct xfs_extent_free_item
{
	xfs_fsblock_t		xefi_startblock;/* starting fs block number */
	xfs_extlen_t		xefi_blockcount;/* number of blocks in extent */
	struct list_head	xefi_list;
	struct xfs_owner_info	xefi_oinfo;	/* extent owner */
};
enum shift_direction {
	SHIFT_LEFT = 0,
	SHIFT_RIGHT,
};
enum xfs_bmap_intent_type {
	XFS_BMAP_MAP = 1,
	XFS_BMAP_UNMAP,
};

struct xfs_bmap_intent {
	struct list_head			bi_list;
	enum xfs_bmap_intent_type		bi_type;
	struct xfs_inode			*bi_owner;
	int					bi_whichfork;
	struct xfs_bmbt_irec1			bi_bmap;
};

const struct xfs_dir_ops *xfs_dir_get_ops(struct xfs_mount    *mp,
                                          struct xfs_inode    *dp);
#endif	/* __XFS_BMAP_H__ */
