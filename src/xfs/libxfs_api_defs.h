/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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

#ifndef __LIBXFS_API_DEFS_H__
#define __LIBXFS_API_DEFS_H__

/*
 * This file defines all the kernel based functions we expose to userspace
 * via the libxfs_* namespace. This is kept in a separate header file so
 * it can be included in both the internal and external libxfs header files
 * without introducing any depenencies between the two.
 */
#define xfs_highbit32			libxfs_highbit32
#define xfs_highbit64			libxfs_highbit64

#define xfs_fs_cmn_err			libxfs_fs_cmn_err

#define xfs_trans_add_item		libxfs_trans_add_item
#define xfs_trans_binval		libxfs_trans_binval
#define xfs_trans_brelse		libxfs_trans_brelse
#define xfs_trans_cancel		libxfs_trans_cancel
#define xfs_trans_del_item		libxfs_trans_del_item
#define xfs_trans_get_buf		libxfs_trans_get_buf
#define xfs_trans_ichgtime		libxfs_trans_ichgtime
#define xfs_trans_log_buf		libxfs_trans_log_buf
#define xfs_trans_log_inode		libxfs_trans_log_inode
#define xfs_trans_read_buf		libxfs_trans_read_buf
#define xfs_trans_resv_calc		libxfs_trans_resv_calc

#define xfs_attr_get			libxfs_attr_get
#define xfs_attr_remove			libxfs_attr_remove
#define xfs_attr_leaf_newentsize	libxfs_attr_leaf_newentsize

#define xfs_alloc_min_freelist		libxfs_alloc_min_freelist
#define xfs_defer_init			libxfs_defer_init
#define xfs_defer_cancel		libxfs_defer_cancel

#define xfs_da_brelse			libxfs_da_brelse
#define xfs_da_hashname			libxfs_da_hashname
#define __xfs_dir2_data_freescan	libxfs_dir2_data_freescan
#define xfs_dir2_data_log_entry		libxfs_dir2_data_log_entry
#define xfs_dir2_data_log_header	libxfs_dir2_data_log_header
#define xfs_dir2_data_make_free		libxfs_dir2_data_make_free
#define xfs_dir2_data_use_free		libxfs_dir2_data_use_free
#define xfs_dir2_shrink_inode		libxfs_dir2_shrink_inode

#define xfs_inode_from_disk		libxfs_inode_from_disk
#define xfs_inode_to_disk		libxfs_inode_to_disk
#define xfs_dinode_calc_crc		libxfs_dinode_calc_crc
#define xfs_idata_realloc		libxfs_idata_realloc
#define xfs_idestroy_fork		libxfs_idestroy_fork

#define xfs_rmap_ag_owner		libxfs_rmap_ag_owner
#define xfs_rmap_irec_offset_pack	libxfs_rmap_irec_offset_pack
#define xfs_rmap_irec_offset_unpack	libxfs_rmap_irec_offset_unpack
#define xfs_btree_del_cursor		libxfs_btree_del_cursor
#define xfs_sb_from_disk		libxfs_sb_from_disk
#define xfs_sb_quota_from_disk		libxfs_sb_quota_from_disk
#define xfs_sb_to_disk			libxfs_sb_to_disk

#define xfs_symlink_blocks		libxfs_symlink_blocks
#define xfs_symlink_hdr_ok		libxfs_symlink_hdr_ok

#define xfs_verify_cksum		libxfs_verify_cksum

#define xfs_alloc_ag_max_usable		libxfs_alloc_ag_max_usable
#define xfs_allocbt_maxrecs		libxfs_allocbt_maxrecs
#define xfs_bmbt_maxrecs		libxfs_bmbt_maxrecs
#define xfs_bmdr_maxrecs		libxfs_bmdr_maxrecs
#define xfs_btree_init_block		libxfs_btree_init_block
#define xfs_dir_ino_validate		libxfs_dir_ino_validate
#define xfs_log_calc_minimum_size	libxfs_log_calc_minimum_size
#define xfs_perag_get			libxfs_perag_get
#define xfs_perag_put			libxfs_perag_put
#define xfs_prealloc_blocks		libxfs_prealloc_blocks
#define xfs_dinode_good_version		libxfs_dinode_good_version

#define xfs_refcountbt_init_cursor	libxfs_refcountbt_init_cursor
#define xfs_refcount_lookup_le		libxfs_refcount_lookup_le
#define xfs_refcount_get_rec		libxfs_refcount_get_rec
#define xfs_refc_block			libxfs_refc_block

#endif /* __LIBXFS_API_DEFS_H__ */
