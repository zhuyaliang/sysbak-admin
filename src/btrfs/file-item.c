/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth
 * Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <btrfs/crc32c.h>
#include <btrfs/radix-tree.h>
#include <btrfs/kerncompat.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "utils.h"

#define MAX_CSUM_ITEMS(r,size) ((((BTRFS_LEAF_DATA_SIZE(r) - \
			       sizeof(struct btrfs_item) * 2) / \
			       size) - 1))
static noinline int truncate_one_csum(struct btrfs_trans_handle *trans,
                      struct btrfs_root *root,
                      struct btrfs_path *path,
                      struct btrfs_key *key,
                      u64 bytenr, u64 len)
{
    struct extent_buffer *leaf;
    u16 csum_size =
        btrfs_super_csum_size(root->fs_info->super_copy);
    u64 csum_end;
    u64 end_byte = bytenr + len;
    u32 blocksize = root->sectorsize;
    int ret;

    leaf = path->nodes[0];
    csum_end = btrfs_item_size_nr(leaf, path->slots[0]) / csum_size;
    csum_end *= root->sectorsize;
    csum_end += key->offset;

    if (key->offset < bytenr && csum_end <= end_byte) {
        u32 new_size = (bytenr - key->offset) / blocksize;
        new_size *= csum_size;
        ret = btrfs_truncate_item(trans, root, path, new_size, 1);
        BUG_ON(ret);
    } else if (key->offset >= bytenr && csum_end > end_byte &&
           end_byte > key->offset) {
        u32 new_size = (csum_end - end_byte) / blocksize;
        new_size *= csum_size;

        ret = btrfs_truncate_item(trans, root, path, new_size, 0);
        BUG_ON(ret);

        key->offset = end_byte;
        ret = btrfs_set_item_key_safe(root, path, key);
        BUG_ON(ret);
    } else {
        BUG();
    }
    return 0;
}

int btrfs_del_csums(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, u64 bytenr, u64 len)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	u64 end_byte = bytenr + len;
	u64 csum_end;
	struct extent_buffer *leaf;
	int ret;
	u16 csum_size =
		btrfs_super_csum_size(root->fs_info->super_copy);
	int blocksize = root->sectorsize;

	root = root->fs_info->csum_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (1) {
		key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
		key.offset = end_byte - 1;
		key.type = BTRFS_EXTENT_CSUM_KEY;

		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0) {
			if (path->slots[0] == 0)
				goto out;
			path->slots[0]--;
		}
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

		if (key.objectid != BTRFS_EXTENT_CSUM_OBJECTID ||
		    key.type != BTRFS_EXTENT_CSUM_KEY) {
			break;
		}

		if (key.offset >= end_byte)
			break;

		csum_end = btrfs_item_size_nr(leaf, path->slots[0]) / csum_size;
		csum_end *= blocksize;
		csum_end += key.offset;

		/* this csum ends before we start, we're done */
		if (csum_end <= bytenr)
			break;

		/* delete the entire item, it is inside our range */
		if (key.offset >= bytenr && csum_end <= end_byte) {
			ret = btrfs_del_item(trans, root, path);
			BUG_ON(ret);
		} else if (key.offset < bytenr && csum_end > end_byte) {
			unsigned long offset;
			unsigned long shift_len;
			unsigned long item_offset;
			offset = (bytenr - key.offset) / blocksize;
			offset *= csum_size;

			shift_len = (len / blocksize) * csum_size;

			item_offset = btrfs_item_ptr_offset(leaf,
							    path->slots[0]);

			memset_extent_buffer(leaf, 0, item_offset + offset,
					     shift_len);
			key.offset = bytenr;

			ret = btrfs_split_item(trans, root, path, &key, offset);
			BUG_ON(ret && ret != -EAGAIN);

			key.offset = end_byte - 1;
		} else {
			ret = truncate_one_csum(trans, root, path,
						&key, bytenr, len);
			BUG_ON(ret);
		}
		btrfs_release_path(path);
	}
out:
	btrfs_free_path(path);
	return 0;
}
