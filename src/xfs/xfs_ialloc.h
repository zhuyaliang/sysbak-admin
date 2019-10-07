/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_IALLOC_H__
#define	__XFS_IALLOC_H__

struct xfs_buf;
struct xfs_dinode;
struct xfs_imap;
struct xfs_mount;
struct xfs_trans;
struct xfs_btree_cur;

/* Move inodes in clusters of this size */
#define	XFS_INODE_BIG_CLUSTER_SIZE	8192

struct xfs_icluster {
	bool		deleted;	/* record is deleted */
	xfs_ino_t	first_ino;	/* first inode number */
	uint64_t	alloc;		/* inode phys. allocation bitmap for
					 * sparse chunks */
};

/* Calculate and return the number of filesystem blocks per inode cluster */
static inline int
xfs_icluster_size_fsb(
	struct xfs_mount	*mp)
{
	if (mp->m_sb.sb_blocksize >= mp->m_inode_cluster_size)
		return 1;
	return mp->m_inode_cluster_size >> mp->m_sb.sb_blocklog;
}

/*
 * Make an inode pointer out of the buffer/offset.
 */
static inline struct xfs_dinode *
xfs_make_iptr(struct xfs_mount *mp, struct xfs_buf *b, int o)
{
	return xfs_buf_offset(b, o << (mp)->m_sb.sb_inodelog);
}

/*
 * Log specified fields for the ag hdr (inode section)
 */
void
xfs_ialloc_log_agi(
	struct xfs_trans *tp,		/* transaction pointer */
	struct xfs_buf	*bp,		/* allocation group header buffer */
	int		fields);	/* bitmask of fields to log */

/*
 * Read in the allocation group header (inode allocation section)
 */
int					/* error */
xfs_ialloc_read_agi(
	struct xfs_mount *mp,		/* file system mount structure */
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	struct xfs_buf	**bpp);		/* allocation group hdr buf */

/*
 * Read in the allocation group header to initialise the per-ag data
 * in the mount structure
 */
int
xfs_ialloc_pagi_init(
	struct xfs_mount *mp,		/* file system mount structure */
	struct xfs_trans *tp,		/* transaction pointer */
        xfs_agnumber_t  agno);		/* allocation group number */

/*
 * Lookup a record by ino in the btree given by cur.
 */
int xfs_inobt_lookup(struct xfs_btree_cur *cur, xfs_agino_t ino,
		xfs_lookup_t dir, int *stat);

/*
 * Get the data from the pointed-to record.
 */
int xfs_inobt_get_rec(struct xfs_btree_cur *cur,
		xfs_inobt_rec_incore_t *rec, int *stat);

/*
 * Inode chunk initialisation routine
 */
int xfs_ialloc_inode_init(struct xfs_mount *mp, struct xfs_trans *tp,
			  struct list_head *buffer_list, int icount,
			  xfs_agnumber_t agno, xfs_agblock_t agbno,
			  xfs_agblock_t length, unsigned int gen);

int xfs_read_agi(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_agnumber_t agno, struct xfs_buf **bpp);


#endif	/* __XFS_IALLOC_H__ */
