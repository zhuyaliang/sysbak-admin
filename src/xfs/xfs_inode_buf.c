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
#include "libxfs_priv.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include <xfs/xfs_log_format.h>
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"
#include "xfs_ialloc.h"

/*
 * Check that none of the inode's in the buffer have a next
 * unlinked field of 0.
 */
#if defined(DEBUG)
void
xfs_inobp_check(
	xfs_mount_t	*mp,
	xfs_buf_t	*bp)
{
	int		i;
	int		j;
	xfs_dinode_t	*dip;

	j = mp->m_inode_cluster_size >> mp->m_sb.sb_inodelog;

	for (i = 0; i < j; i++) {
		dip = xfs_buf_offset(bp, i * mp->m_sb.sb_inodesize);
		if (!dip->di_next_unlinked)  {
			xfs_alert(mp,
	"Detected bogus zero next_unlinked field in inode %d buffer 0x%llx.",
				i, (long long)bp->b_bn);
		}
	}
}
#endif

bool
xfs_dinode_good_version(
	struct xfs_mount *mp,
	__u8		version)
{
	if (xfs_sb_version_hascrc(&mp->m_sb))
		return version == 3;

	return version == 1 || version == 2;
}

/*
 * If we are doing readahead on an inode buffer, we might be in log recovery
 * reading an inode allocation buffer that hasn't yet been replayed, and hence
 * has not had the inode cores stamped into it. Hence for readahead, the buffer
 * may be potentially invalid.
 *
 * If the readahead buffer is invalid, we need to mark it with an error and
 * clear the DONE status of the buffer so that a followup read will re-read it
 * from disk. We don't report the error otherwise to avoid warnings during log
 * recovery and we don't get unnecssary panics on debug kernels. We use EIO here
 * because all we want to do is say readahead failed; there is no-one to report
 * the error to, so this will distinguish it from a non-ra verifier failure.
 * Changes to this readahead error behavour also need to be reflected in
 * xfs_dquot_buf_readahead_verify().
 */
static void
xfs_inode_buf_verify(
	struct xfs_buf	*bp,
	bool		readahead)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	int		i;
	int		ni;

	/*
	 * Validate the magic number and version of every inode in the buffer
	 */
	ni = XFS_BB_TO_FSB(mp, bp->b_length) * mp->m_sb.sb_inopblock;
	for (i = 0; i < ni; i++) {
		int		di_ok;
		xfs_dinode_t	*dip;

		dip = xfs_buf_offset(bp, (i << mp->m_sb.sb_inodelog));
		di_ok = dip->di_magic == cpu_to_be16(XFS_DINODE_MAGIC) &&
			xfs_dinode_good_version(mp, dip->di_version);
		if (unlikely(XFS_TEST_ERROR(!di_ok, mp,
						XFS_ERRTAG_ITOBP_INOTOBP,
						XFS_RANDOM_ITOBP_INOTOBP))) {
			if (readahead) {
				bp->b_flags &= ~XBF_DONE;
				xfs_buf_ioerror(bp, -EIO);
				return;
			}

			xfs_buf_ioerror(bp, -EFSCORRUPTED);
			xfs_verifier_error(bp);
#ifdef DEBUG
			xfs_alert(mp,
				"bad inode magic/vsn daddr %lld #%d (magic=%x)",
				(unsigned long long)bp->b_bn, i,
				be16_to_cpu(dip->di_magic));
#endif
		}
	}
	xfs_inobp_check(mp, bp);
}


static void
xfs_inode_buf_read_verify(
	struct xfs_buf	*bp)
{
	xfs_inode_buf_verify(bp, false);
}

static void
xfs_inode_buf_readahead_verify(
	struct xfs_buf	*bp)
{
	xfs_inode_buf_verify(bp, true);
}

static void
xfs_inode_buf_write_verify(
	struct xfs_buf	*bp)
{
	xfs_inode_buf_verify(bp, false);
}

const struct xfs_buf_ops xfs_inode_buf_ops = {
	.name = "xfs_inode",
	.verify_read = xfs_inode_buf_read_verify,
	.verify_write = xfs_inode_buf_write_verify,
};

const struct xfs_buf_ops xfs_inode_buf_ra_ops = {
	.name = "xxfs_inode_ra",
	.verify_read = xfs_inode_buf_readahead_verify,
	.verify_write = xfs_inode_buf_write_verify,
};

void
xfs_inode_from_disk(
	struct xfs_inode	*ip,
	struct xfs_dinode	*from)
{
	struct xfs_icdinode	*to = &ip->i_d;
	struct inode		*inode = VFS_I(ip);


	/*
	 * Convert v1 inodes immediately to v2 inode format as this is the
	 * minimum inode version format we support in the rest of the code.
	 */
	to->di_version = from->di_version;
	if (to->di_version == 1) {
		set_nlink(inode, be16_to_cpu(from->di_onlink));
		to->di_projid_lo = 0;
		to->di_projid_hi = 0;
		to->di_version = 2;
	} else {
		set_nlink(inode, be32_to_cpu(from->di_nlink));
		to->di_projid_lo = be16_to_cpu(from->di_projid_lo);
		to->di_projid_hi = be16_to_cpu(from->di_projid_hi);
	}

	to->di_format = from->di_format;
	to->di_uid = be32_to_cpu(from->di_uid);
	to->di_gid = be32_to_cpu(from->di_gid);
	to->di_flushiter = be16_to_cpu(from->di_flushiter);

	/*
	 * Time is signed, so need to convert to signed 32 bit before
	 * storing in inode timestamp which may be 64 bit. Otherwise
	 * a time before epoch is converted to a time long after epoch
	 * on 64 bit systems.
	 */
	inode->i_atime.tv_sec = (int)be32_to_cpu(from->di_atime.t_sec);
	inode->i_atime.tv_nsec = (int)be32_to_cpu(from->di_atime.t_nsec);
	inode->i_mtime.tv_sec = (int)be32_to_cpu(from->di_mtime.t_sec);
	inode->i_mtime.tv_nsec = (int)be32_to_cpu(from->di_mtime.t_nsec);
	inode->i_ctime.tv_sec = (int)be32_to_cpu(from->di_ctime.t_sec);
	inode->i_ctime.tv_nsec = (int)be32_to_cpu(from->di_ctime.t_nsec);
	inode->i_generation = be32_to_cpu(from->di_gen);
	inode->i_mode = be16_to_cpu(from->di_mode);

	to->di_size = be64_to_cpu(from->di_size);
	to->di_nblocks = be64_to_cpu(from->di_nblocks);
	to->di_extsize = be32_to_cpu(from->di_extsize);
	to->di_nextents = be32_to_cpu(from->di_nextents);
	to->di_anextents = be16_to_cpu(from->di_anextents);
	to->di_forkoff = from->di_forkoff;
	to->di_aformat	= from->di_aformat;
	to->di_dmevmask	= be32_to_cpu(from->di_dmevmask);
	to->di_dmstate	= be16_to_cpu(from->di_dmstate);
	to->di_flags	= be16_to_cpu(from->di_flags);

	if (to->di_version == 3) {
		inode->i_version = be64_to_cpu(from->di_changecount);
		to->di_crtime.t_sec = be32_to_cpu(from->di_crtime.t_sec);
		to->di_crtime.t_nsec = be32_to_cpu(from->di_crtime.t_nsec);
		to->di_flags2 = be64_to_cpu(from->di_flags2);
		to->di_cowextsize = be32_to_cpu(from->di_cowextsize);
	}
}

void
xfs_inode_to_disk(
	struct xfs_inode	*ip,
	struct xfs_dinode	*to,
	xfs_lsn_t		lsn)
{
	struct xfs_icdinode	*from = &ip->i_d;
	struct inode		*inode = VFS_I(ip);

	to->di_magic = cpu_to_be16(XFS_DINODE_MAGIC);
	to->di_onlink = 0;

	to->di_version = from->di_version;
	to->di_format = from->di_format;
	to->di_uid = cpu_to_be32(from->di_uid);
	to->di_gid = cpu_to_be32(from->di_gid);
	to->di_projid_lo = cpu_to_be16(from->di_projid_lo);
	to->di_projid_hi = cpu_to_be16(from->di_projid_hi);

	memset(to->di_pad, 0, sizeof(to->di_pad));
	to->di_atime.t_sec = cpu_to_be32(inode->i_atime.tv_sec);
	to->di_atime.t_nsec = cpu_to_be32(inode->i_atime.tv_nsec);
	to->di_mtime.t_sec = cpu_to_be32(inode->i_mtime.tv_sec);
	to->di_mtime.t_nsec = cpu_to_be32(inode->i_mtime.tv_nsec);
	to->di_ctime.t_sec = cpu_to_be32(inode->i_ctime.tv_sec);
	to->di_ctime.t_nsec = cpu_to_be32(inode->i_ctime.tv_nsec);
	to->di_nlink = cpu_to_be32(inode->i_nlink);
	to->di_gen = cpu_to_be32(inode->i_generation);
	to->di_mode = cpu_to_be16(inode->i_mode);

	to->di_size = cpu_to_be64(from->di_size);
	to->di_nblocks = cpu_to_be64(from->di_nblocks);
	to->di_extsize = cpu_to_be32(from->di_extsize);
	to->di_nextents = cpu_to_be32(from->di_nextents);
	to->di_anextents = cpu_to_be16(from->di_anextents);
	to->di_forkoff = from->di_forkoff;
	to->di_aformat = from->di_aformat;
	to->di_dmevmask = cpu_to_be32(from->di_dmevmask);
	to->di_dmstate = cpu_to_be16(from->di_dmstate);
	to->di_flags = cpu_to_be16(from->di_flags);

	if (from->di_version == 3) {
		to->di_changecount = cpu_to_be64(inode->i_version);
		to->di_crtime.t_sec = cpu_to_be32(from->di_crtime.t_sec);
		to->di_crtime.t_nsec = cpu_to_be32(from->di_crtime.t_nsec);
		to->di_flags2 = cpu_to_be64(from->di_flags2);
		to->di_cowextsize = cpu_to_be32(from->di_cowextsize);
		to->di_ino = cpu_to_be64(ip->i_ino);
		to->di_lsn = cpu_to_be64(lsn);
		memset(to->di_pad2, 0, sizeof(to->di_pad2));
		uuid_copy(&to->di_uuid, &ip->i_mount->m_sb.sb_meta_uuid);
		to->di_flushiter = 0;
	} else {
		to->di_flushiter = cpu_to_be16(from->di_flushiter);
	}
}

void
xfs_log_dinode_to_disk(
	struct xfs_log_dinode	*from,
	struct xfs_dinode	*to)
{
	to->di_magic = cpu_to_be16(from->di_magic);
	to->di_mode = cpu_to_be16(from->di_mode);
	to->di_version = from->di_version;
	to->di_format = from->di_format;
	to->di_onlink = 0;
	to->di_uid = cpu_to_be32(from->di_uid);
	to->di_gid = cpu_to_be32(from->di_gid);
	to->di_nlink = cpu_to_be32(from->di_nlink);
	to->di_projid_lo = cpu_to_be16(from->di_projid_lo);
	to->di_projid_hi = cpu_to_be16(from->di_projid_hi);
	memcpy(to->di_pad, from->di_pad, sizeof(to->di_pad));

	to->di_atime.t_sec = cpu_to_be32(from->di_atime.t_sec);
	to->di_atime.t_nsec = cpu_to_be32(from->di_atime.t_nsec);
	to->di_mtime.t_sec = cpu_to_be32(from->di_mtime.t_sec);
	to->di_mtime.t_nsec = cpu_to_be32(from->di_mtime.t_nsec);
	to->di_ctime.t_sec = cpu_to_be32(from->di_ctime.t_sec);
	to->di_ctime.t_nsec = cpu_to_be32(from->di_ctime.t_nsec);

	to->di_size = cpu_to_be64(from->di_size);
	to->di_nblocks = cpu_to_be64(from->di_nblocks);
	to->di_extsize = cpu_to_be32(from->di_extsize);
	to->di_nextents = cpu_to_be32(from->di_nextents);
	to->di_anextents = cpu_to_be16(from->di_anextents);
	to->di_forkoff = from->di_forkoff;
	to->di_aformat = from->di_aformat;
	to->di_dmevmask = cpu_to_be32(from->di_dmevmask);
	to->di_dmstate = cpu_to_be16(from->di_dmstate);
	to->di_flags = cpu_to_be16(from->di_flags);
	to->di_gen = cpu_to_be32(from->di_gen);

	if (from->di_version == 3) {
		to->di_changecount = cpu_to_be64(from->di_changecount);
		to->di_crtime.t_sec = cpu_to_be32(from->di_crtime.t_sec);
		to->di_crtime.t_nsec = cpu_to_be32(from->di_crtime.t_nsec);
		to->di_flags2 = cpu_to_be64(from->di_flags2);
		to->di_cowextsize = cpu_to_be32(from->di_cowextsize);
		to->di_ino = cpu_to_be64(from->di_ino);
		to->di_lsn = cpu_to_be64(from->di_lsn);
		memcpy(to->di_pad2, from->di_pad2, sizeof(to->di_pad2));
		uuid_copy(&to->di_uuid, &from->di_uuid);
		to->di_flushiter = 0;
	} else {
		to->di_flushiter = cpu_to_be16(from->di_flushiter);
	}
}

bool
xfs_dinode_verify(
	struct xfs_mount	*mp,
	xfs_ino_t		ino,
	struct xfs_dinode	*dip)
{
	uint16_t		flags;
	uint64_t		flags2;

	if (dip->di_magic != cpu_to_be16(XFS_DINODE_MAGIC))
		return false;

	/* only version 3 or greater inodes are extensively verified here */
	if (dip->di_version < 3)
		return true;

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return false;
	if (!xfs_verify_cksum((char *)dip, mp->m_sb.sb_inodesize,
			      XFS_DINODE_CRC_OFF))
		return false;
	if (be64_to_cpu(dip->di_ino) != ino)
		return false;
	if (!uuid_equal(&dip->di_uuid, &mp->m_sb.sb_meta_uuid))
		return false;

	flags = be16_to_cpu(dip->di_flags);
	flags2 = be64_to_cpu(dip->di_flags2);

	/* don't allow reflink/cowextsize if we don't have reflink */
	if ((flags2 & (XFS_DIFLAG2_REFLINK | XFS_DIFLAG2_COWEXTSIZE)) &&
            !xfs_sb_version_hasreflink(&mp->m_sb))
		return false;

	/* don't let reflink and realtime mix */
	if ((flags2 & XFS_DIFLAG2_REFLINK) && (flags & XFS_DIFLAG_REALTIME))
		return false;

	/* don't let reflink and dax mix */
	if ((flags2 & XFS_DIFLAG2_REFLINK) && (flags2 & XFS_DIFLAG2_DAX))
		return false;

	return true;
}

void
xfs_dinode_calc_crc(
	struct xfs_mount	*mp,
	struct xfs_dinode	*dip)
{
	__uint32_t		crc;

	if (dip->di_version < 3)
		return;

	ASSERT(xfs_sb_version_hascrc(&mp->m_sb));
	crc = xfs_start_cksum((char *)dip, mp->m_sb.sb_inodesize,
			      XFS_DINODE_CRC_OFF);
	dip->di_crc = xfs_end_cksum(crc);
}
