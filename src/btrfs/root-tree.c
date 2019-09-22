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

#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"

int btrfs_find_last_root(struct btrfs_root *root, u64 objectid,
			struct btrfs_root_item *item, struct btrfs_key *key)
{
	struct btrfs_path *path;
	struct btrfs_key search_key;
	struct btrfs_key found_key;
	struct extent_buffer *l;
	int ret;
	int slot;

	search_key.objectid = objectid;
	search_key.type = BTRFS_ROOT_ITEM_KEY;
	search_key.offset = (u64)-1;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (path->slots[0] == 0) {
		ret = -ENOENT;
		goto out;
	}

	BUG_ON(ret == 0);
	l = path->nodes[0];
	slot = path->slots[0] - 1;
	btrfs_item_key_to_cpu(l, &found_key, slot);
	if (found_key.objectid != objectid) {
		ret = -ENOENT;
		goto out;
	}
	read_extent_buffer(l, item, btrfs_item_ptr_offset(l, slot),
			   sizeof(*item));
	memcpy(key, &found_key, sizeof(found_key));
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_update_root(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_root_item
		      *item)
{
	struct btrfs_path *path;
	struct extent_buffer *l;
	int ret;
	int slot;
	unsigned long ptr;
	u32 old_len;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(trans, root, key, path, 0, 1);
	if (ret < 0)
		goto out;
	BUG_ON(ret != 0);
	l = path->nodes[0];
	slot = path->slots[0];
	ptr = btrfs_item_ptr_offset(l, slot);
	old_len = btrfs_item_size_nr(l, slot);

	/*
	 * If this is the first time we update the root item which originated
	 * from an older kernel, we need to enlarge the item size to make room
	 * for the added fields.
	 */
	if (old_len < sizeof(*item)) {
		btrfs_release_path(path);
		ret = btrfs_search_slot(trans, root, key, path,
				-1, 1);
		if (ret < 0) {
			goto out;
		}

		ret = btrfs_del_item(trans, root, path);
		if (ret < 0) {
			goto out;
		}
		btrfs_release_path(path);
		ret = btrfs_insert_empty_item(trans, root, path,
				key, sizeof(*item));
		if (ret < 0) {
			goto out;
		}
		l = path->nodes[0];
		slot = path->slots[0];
		ptr = btrfs_item_ptr_offset(l, slot);
	}

	/*
	 * Update generation_v2 so at the next mount we know the new root
	 * fields are valid.
	 */
	btrfs_set_root_generation_v2(item, btrfs_root_generation(item));

	write_extent_buffer(l, item, ptr, sizeof(*item));
	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_free_path(path);
	return ret;
}

