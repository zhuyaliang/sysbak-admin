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
#include "disk-io.h"
#include "transaction.h"
#include "utils.h"

struct btrfs_corrupt_block {
	struct cache_extent cache;
	struct btrfs_key key;
	int level;
};
static int split_node(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, int level);
              
static int split_leaf(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *ins_key,
		      struct btrfs_path *path, int data_size, int extend);

static int push_node_left(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *dst,
			  struct extent_buffer *src, int empty);

static int balance_node_right(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct extent_buffer *dst_buf,
			      struct extent_buffer *src_buf);

inline void btrfs_init_path(struct btrfs_path *p)
{
	memset(p, 0, sizeof(*p));
}

struct btrfs_path *btrfs_alloc_path(void)
{
	struct btrfs_path *path;
	path = kzalloc(sizeof(struct btrfs_path), GFP_NOFS);
	return path;
}

void btrfs_free_path(struct btrfs_path *p)
{
	if (!p)
		return;
	btrfs_release_path(p);
	kfree(p);
}

void btrfs_release_path(struct btrfs_path *p)
{
	int i;
	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		if (!p->nodes[i])
			continue;
		free_extent_buffer(p->nodes[i]);
	}
	memset(p, 0, sizeof(*p));
}

void add_root_to_dirty_list(struct btrfs_root *root)
{
	if (root->track_dirty && list_empty(&root->dirty_list)) {
		list_add(&root->dirty_list,
			 &root->fs_info->dirty_cowonly_roots);
	}
}

static int btrfs_block_can_be_shared(struct btrfs_root *root,
			             struct extent_buffer *buf)
{
	if (root->ref_cows &&
	    buf != root->node && buf != root->commit_root &&
	    (btrfs_header_generation(buf) <=
	     btrfs_root_last_snapshot(&root->root_item) ||
	     btrfs_header_flag(buf, BTRFS_HEADER_FLAG_RELOC)))
		return 1;
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
        if (root->ref_cows &&
            btrfs_header_backref_rev(buf) < BTRFS_MIXED_BACKREF_REV)
                return 1;
#endif
	return 0;
}

static noinline int update_ref_for_cow(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root,
				       struct extent_buffer *buf,
				       struct extent_buffer *cow)
{
	u64 refs;
	u64 owner;
	u64 flags;
	u64 new_flags = 0;
	int ret;

	if (btrfs_block_can_be_shared(root, buf)) {
		ret = btrfs_lookup_extent_info(trans, root, buf->start,
					       btrfs_header_level(buf), 1,
					       &refs, &flags);
		if (ret)
        {
            return -1;
        }    
		if (refs == 0)
        {
            return -1;
        }    
	} else {
		refs = 1;
		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID ||
		    btrfs_header_backref_rev(buf) < BTRFS_MIXED_BACKREF_REV)
			flags = BTRFS_BLOCK_FLAG_FULL_BACKREF;
		else
			flags = 0;
	}

	owner = btrfs_header_owner(buf);
	if (!(flags & BTRFS_BLOCK_FLAG_FULL_BACKREF) &&
	       owner == BTRFS_TREE_RELOC_OBJECTID)
    {
        return -1;
    }    

	if (refs > 1) {
		if ((owner == root->root_key.objectid ||
		     root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) &&
		    !(flags & BTRFS_BLOCK_FLAG_FULL_BACKREF)) {
			ret = btrfs_inc_ref(trans, root, buf, 1);
			if (ret)
            {
                return -1;
            }    

			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID) {
				ret = btrfs_dec_ref(trans, root, buf, 0);
				if (ret)
                {
                    return -1;
                }    
				ret = btrfs_inc_ref(trans, root, cow, 1);
				if (ret)
                {
                    return -1;
                }    
			}
			new_flags |= BTRFS_BLOCK_FLAG_FULL_BACKREF;
		} else {

			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID)
				ret = btrfs_inc_ref(trans, root, cow, 1);
			else
				ret = btrfs_inc_ref(trans, root, cow, 0);
		}
		if (new_flags != 0) {
			ret = btrfs_set_block_flags(trans, root, buf->start,
						    btrfs_header_level(buf),
						    new_flags);
			if (ret)
            {
                return -1;
            }    
		}
	} else {
		if (flags & BTRFS_BLOCK_FLAG_FULL_BACKREF) {
			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID)
				ret = btrfs_inc_ref(trans, root, cow, 1);
			else
				ret = btrfs_inc_ref(trans, root, cow, 0);
			ret = btrfs_dec_ref(trans, root, buf, 1);
			if (ret)
            {
                return -1;
            }    
		}
		clean_tree_block(trans, root, buf);
	}
	return 0;
}

int __btrfs_cow_block(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct extent_buffer *buf,
			     struct extent_buffer *parent, int parent_slot,
			     struct extent_buffer **cow_ret,
			     u64 search_start, u64 empty_size)
{
	struct extent_buffer *cow;
	struct btrfs_disk_key disk_key;
	int level;

	WARN_ON(root->ref_cows && trans->transid !=
		root->fs_info->running_transaction->transid);
	WARN_ON(root->ref_cows && trans->transid != root->last_trans);

	level = btrfs_header_level(buf);

	if (level == 0)
		btrfs_item_key(buf, &disk_key, 0);
	else
		btrfs_node_key(buf, &disk_key, 0);

	cow = btrfs_alloc_free_block(trans, root, buf->len,
				     root->root_key.objectid, &disk_key,
				     level, search_start, empty_size);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	copy_extent_buffer(cow, buf, 0, 0, cow->len);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_backref_rev(cow, BTRFS_MIXED_BACKREF_REV);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN |
				     BTRFS_HEADER_FLAG_RELOC);
	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID)
		btrfs_set_header_flag(cow, BTRFS_HEADER_FLAG_RELOC);
	else
		btrfs_set_header_owner(cow, root->root_key.objectid);

	write_extent_buffer(cow, root->fs_info->fsid,
			    btrfs_header_fsid(), BTRFS_FSID_SIZE);

	WARN_ON(!(buf->flags & EXTENT_BAD_TRANSID) &&
		btrfs_header_generation(buf) > trans->transid);

	update_ref_for_cow(trans, root, buf, cow);

	if (buf == root->node) {
		root->node = cow;
		extent_buffer_get(cow);

		btrfs_free_extent(trans, root, buf->start, buf->len,
				  0, root->root_key.objectid, level, 0);
		free_extent_buffer(buf);
		add_root_to_dirty_list(root);
	} else {
		btrfs_set_node_blockptr(parent, parent_slot,
					cow->start);
		WARN_ON(trans->transid == 0);
		btrfs_set_node_ptr_generation(parent, parent_slot,
					      trans->transid);
		btrfs_mark_buffer_dirty(parent);
		WARN_ON(btrfs_header_generation(parent) != trans->transid);

		btrfs_free_extent(trans, root, buf->start, buf->len,
				  0, root->root_key.objectid, level, 1);
	}
	if (!list_empty(&buf->recow)) {
		list_del_init(&buf->recow);
		free_extent_buffer(buf);
	}
	free_extent_buffer(buf);
	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

static inline int should_cow_block(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct extent_buffer *buf)
{
	if (btrfs_header_generation(buf) == trans->transid &&
	    !btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN) &&
	    !(root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID &&
	      btrfs_header_flag(buf, BTRFS_HEADER_FLAG_RELOC)))
		return 0;
	return 1;
}

int btrfs_cow_block(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct extent_buffer *buf,
		    struct extent_buffer *parent, int parent_slot,
		    struct extent_buffer **cow_ret)
{
	u64 search_start;
	int ret;
	if (trans->transid != root->fs_info->generation) {
		WARN_ON(1);
	}
	if (!should_cow_block(trans, root, buf)) {
		*cow_ret = buf;
		return 0;
	}

	search_start = buf->start & ~((u64)(1024 * 1024 * 1024) - 1);
	ret = __btrfs_cow_block(trans, root, buf, parent,
				 parent_slot, cow_ret, search_start, 0);
	return ret;
}

int btrfs_comp_cpu_keys(struct btrfs_key *k1, struct btrfs_key *k2)
{
	if (k1->objectid > k2->objectid)
		return 1;
	if (k1->objectid < k2->objectid)
		return -1;
	if (k1->type > k2->type)
		return 1;
	if (k1->type < k2->type)
		return -1;
	if (k1->offset > k2->offset)
		return 1;
	if (k1->offset < k2->offset)
		return -1;
	return 0;
}

static int btrfs_comp_keys(struct btrfs_disk_key *disk, struct btrfs_key *k2)
{
	struct btrfs_key k1;

	btrfs_disk_key_to_cpu(&k1, disk);
	return btrfs_comp_cpu_keys(&k1, k2);
}


static inline unsigned int leaf_data_end(struct btrfs_root *root,
					 struct extent_buffer *leaf)
{
	u32 nr = btrfs_header_nritems(leaf);
	if (nr == 0)
		return BTRFS_LEAF_DATA_SIZE(root);
	return btrfs_item_offset_nr(leaf, nr - 1);
}

static int btrfs_add_corrupt_extent_record(struct btrfs_fs_info *info,
				                           struct btrfs_key *first_key,
				                           u64 start, u64 len, int level)

{
	int ret = 0;
	struct btrfs_corrupt_block *corrupt;

	if (!info->corrupt_blocks)
		return 0;

	corrupt = malloc(sizeof(*corrupt));
	if (!corrupt)
		return -ENOMEM;

	memcpy(&corrupt->key, first_key, sizeof(*first_key));
	corrupt->cache.start = start;
	corrupt->cache.size = len;
	corrupt->level = level;

	ret = insert_cache_extent(info->corrupt_blocks, &corrupt->cache);
	if (ret)
		free(corrupt);
    if (ret && ret != -EEXIST)
    {
        return -1;
    }    
	return ret;
}
enum btrfs_tree_block_status
btrfs_check_node(struct btrfs_root *root, struct btrfs_disk_key *parent_key,
		 struct extent_buffer *buf)
{
	uint i;
	struct btrfs_key cpukey;
	struct btrfs_disk_key key;
	u32 nritems = btrfs_header_nritems(buf);
	enum btrfs_tree_block_status ret = BTRFS_TREE_BLOCK_INVALID_NRITEMS;

	if (nritems == 0 || nritems > BTRFS_NODEPTRS_PER_BLOCK(root))
		goto fail;

	ret = BTRFS_TREE_BLOCK_INVALID_PARENT_KEY;
	if (parent_key && parent_key->type) {
		btrfs_node_key(buf, &key, 0);
		if (memcmp(parent_key, &key, sizeof(key)))
			goto fail;
	}
	ret = BTRFS_TREE_BLOCK_BAD_KEY_ORDER;
	for (i = 0; nritems > 1 && i < nritems - 2; i++) {
		btrfs_node_key(buf, &key, i);
		btrfs_node_key_to_cpu(buf, &cpukey, i + 1);
		if (btrfs_comp_keys(&key, &cpukey) >= 0)
			goto fail;
	}
	return BTRFS_TREE_BLOCK_CLEAN;
fail:
	if (btrfs_header_owner(buf) == BTRFS_EXTENT_TREE_OBJECTID) {
		if (parent_key)
			btrfs_disk_key_to_cpu(&cpukey, parent_key);
		else
			btrfs_node_key_to_cpu(buf, &cpukey, 0);
		btrfs_add_corrupt_extent_record(root->fs_info, &cpukey,
						buf->start, buf->len,
						btrfs_header_level(buf));
	}
	return ret;
}

enum btrfs_tree_block_status
btrfs_check_leaf(struct btrfs_root *root, struct btrfs_disk_key *parent_key,
		 struct extent_buffer *buf)
{
	uint i;
	struct btrfs_key cpukey;
	struct btrfs_disk_key key;
	u32 nritems = btrfs_header_nritems(buf);
	enum btrfs_tree_block_status ret = BTRFS_TREE_BLOCK_INVALID_NRITEMS;

	if (nritems * sizeof(struct btrfs_item) > buf->len)  {
		goto fail;
	}

	if (btrfs_header_level(buf) != 0) {
		ret = BTRFS_TREE_BLOCK_INVALID_LEVEL;
		goto fail;
	}
	if (btrfs_leaf_free_space(root, buf) < 0) {
		ret = BTRFS_TREE_BLOCK_INVALID_FREE_SPACE;
		goto fail;
	}

	if (nritems == 0)
		return BTRFS_TREE_BLOCK_CLEAN;

	btrfs_item_key(buf, &key, 0);
	if (parent_key && parent_key->type &&
	    memcmp(parent_key, &key, sizeof(key))) {
		ret = BTRFS_TREE_BLOCK_INVALID_PARENT_KEY;
		goto fail;
	}
	for (i = 0; nritems > 1 && i < nritems - 1; i++) {
		btrfs_item_key(buf, &key, i);
		btrfs_item_key_to_cpu(buf, &cpukey, i + 1);
		if (btrfs_comp_keys(&key, &cpukey) >= 0) {
			ret = BTRFS_TREE_BLOCK_BAD_KEY_ORDER;
			goto fail;
		}
		if (btrfs_item_offset_nr(buf, i) !=
			btrfs_item_end_nr(buf, i + 1)) {
			ret = BTRFS_TREE_BLOCK_INVALID_OFFSETS;
			goto fail;
		}
		if (i == 0 && btrfs_item_end_nr(buf, i) !=
		    BTRFS_LEAF_DATA_SIZE(root)) {
			ret = BTRFS_TREE_BLOCK_INVALID_OFFSETS;
			goto fail;
		}
	}

	for (i = 0; i < nritems; i++) {
		if (btrfs_item_end_nr(buf, i) > BTRFS_LEAF_DATA_SIZE(root)) {
			btrfs_item_key(buf, &key, 0);
			fflush(stdout);
			ret = BTRFS_TREE_BLOCK_INVALID_OFFSETS;
			goto fail;
		}
	}

	return BTRFS_TREE_BLOCK_CLEAN;
fail:
	if (btrfs_header_owner(buf) == BTRFS_EXTENT_TREE_OBJECTID) {
		if (parent_key)
			btrfs_disk_key_to_cpu(&cpukey, parent_key);
		else
			btrfs_item_key_to_cpu(buf, &cpukey, 0);

		btrfs_add_corrupt_extent_record(root->fs_info, &cpukey,
						buf->start, buf->len, 0);
	}
	return ret;
}

static int noinline check_block(struct btrfs_root *root,
				struct btrfs_path *path, int level)
{
	struct btrfs_disk_key key;
	struct btrfs_disk_key *key_ptr = NULL;
	struct extent_buffer *parent;
	enum btrfs_tree_block_status ret;

	if (path->skip_check_block)
		return 0;
	if (path->nodes[level + 1]) {
		parent = path->nodes[level + 1];
		btrfs_node_key(parent, &key, path->slots[level + 1]);
		key_ptr = &key;
	}
	if (level == 0)
		ret =  btrfs_check_leaf(root, key_ptr, path->nodes[0]);
	else
		ret = btrfs_check_node(root, key_ptr, path->nodes[level]);
	if (ret == BTRFS_TREE_BLOCK_CLEAN)
		return 0;
	return -EIO;
}

static int generic_bin_search(struct extent_buffer *eb, unsigned long p,
			      int item_size, struct btrfs_key *key,
			      int max, int *slot)
{
	int low = 0;
	int high = max;
	int mid;
	int ret;
	unsigned long offset;
	struct btrfs_disk_key *tmp;

	while(low < high) {
		mid = (low + high) / 2;
		offset = p + mid * item_size;

		tmp = (struct btrfs_disk_key *)(eb->data + offset);
		ret = btrfs_comp_keys(tmp, key);

		if (ret < 0)
			low = mid + 1;
		else if (ret > 0)
			high = mid;
		else {
			*slot = mid;
			return 0;
		}
	}
	*slot = low;
	return 1;
}

static int bin_search(struct extent_buffer *eb, struct btrfs_key *key,
		      int level, int *slot)
{
	if (level == 0)
		return generic_bin_search(eb,
					  offsetof(struct btrfs_leaf, items),
					  sizeof(struct btrfs_item),
					  key, btrfs_header_nritems(eb),
					  slot);
	else
		return generic_bin_search(eb,
					  offsetof(struct btrfs_node, ptrs),
					  sizeof(struct btrfs_key_ptr),
					  key, btrfs_header_nritems(eb),
					  slot);
}

struct extent_buffer *read_node_slot(struct btrfs_root *root,
				   struct extent_buffer *parent, int slot)
{
	int level = btrfs_header_level(parent);
	if (slot < 0)
		return NULL;
	if (slot >= (int)btrfs_header_nritems(parent))
		return NULL;

	if (level == 0)
		return NULL;

	return read_tree_block(root, btrfs_node_blockptr(parent, slot),
		       root->nodesize,
		       btrfs_node_ptr_generation(parent, slot));
}

static int balance_level(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 struct btrfs_path *path, int level)
{
	struct extent_buffer *right = NULL;
	struct extent_buffer *mid;
	struct extent_buffer *left = NULL;
	struct extent_buffer *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];
	u64 orig_ptr;

	if (level == 0)
		return 0;

	mid = path->nodes[level];
	WARN_ON(btrfs_header_generation(mid) != trans->transid);

	orig_ptr = btrfs_node_blockptr(mid, orig_slot);

	if (level < BTRFS_MAX_LEVEL - 1) {
		parent = path->nodes[level + 1];
		pslot = path->slots[level + 1];
	}

	if (!parent) {
		struct extent_buffer *child;

		if (btrfs_header_nritems(mid) != 1)
			return 0;

		child = read_node_slot(root, mid, 0);
		if (!extent_buffer_uptodate(child))
        {
            return -1;
        }    
		ret = btrfs_cow_block(trans, root, child, mid, 0, &child);
		if (ret)
        {
            return -1;
        }    

		root->node = child;
		add_root_to_dirty_list(root);
		path->nodes[level] = NULL;
		clean_tree_block(trans, root, mid);
		wait_on_tree_block_writeback(root, mid);
		free_extent_buffer(mid);

		ret = btrfs_free_extent(trans, root, mid->start, mid->len,
					0, root->root_key.objectid,
					level, 1);
		free_extent_buffer(mid);
		return ret;
	}
	if (btrfs_header_nritems(mid) >
	    BTRFS_NODEPTRS_PER_BLOCK(root) / 4)
		return 0;

	left = read_node_slot(root, parent, pslot - 1);
	if (extent_buffer_uptodate(left)) {
		wret = btrfs_cow_block(trans, root, left,
				       parent, pslot - 1, &left);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}
	right = read_node_slot(root, parent, pslot + 1);
	if (extent_buffer_uptodate(right)) {
		wret = btrfs_cow_block(trans, root, right,
				       parent, pslot + 1, &right);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}

	if (left) {
		orig_slot += btrfs_header_nritems(left);
		wret = push_node_left(trans, root, left, mid, 1);
		if (wret < 0)
			ret = wret;
	}

	if (right) {
		wret = push_node_left(trans, root, mid, right, 1);
		if (wret < 0 && wret != -ENOSPC)
			ret = wret;
		if (btrfs_header_nritems(right) == 0) {
			u64 bytenr = right->start;
			u32 blocksize = right->len;

			clean_tree_block(trans, root, right);
			wait_on_tree_block_writeback(root, right);
			free_extent_buffer(right);
			right = NULL;
			wret = btrfs_del_ptr(trans, root, path,
					     level + 1, pslot + 1);
			if (wret)
				ret = wret;
			wret = btrfs_free_extent(trans, root, bytenr,
						 blocksize, 0,
						 root->root_key.objectid,
						 level, 0);
			if (wret)
				ret = wret;
		} else {
			struct btrfs_disk_key right_key;
			btrfs_node_key(right, &right_key, 0);
			btrfs_set_node_key(parent, &right_key, pslot + 1);
			btrfs_mark_buffer_dirty(parent);
		}
	}
	if (btrfs_header_nritems(mid) == 1) {
		if (!left)
        {
            return -1;
        }    
		wret = balance_node_right(trans, root, mid, left);
		if (wret < 0) {
			ret = wret;
			goto enospc;
		}
		if (wret == 1) {
			wret = push_node_left(trans, root, left, mid, 1);
			if (wret < 0)
				ret = wret;
		}
		if (wret == 1)
        {
            return -1;
        }    
	}
	if (btrfs_header_nritems(mid) == 0) {
		u64 bytenr = mid->start;
		u32 blocksize = mid->len;
		clean_tree_block(trans, root, mid);
		wait_on_tree_block_writeback(root, mid);
		free_extent_buffer(mid);
		mid = NULL;
		wret = btrfs_del_ptr(trans, root, path, level + 1, pslot);
		if (wret)
			ret = wret;
		wret = btrfs_free_extent(trans, root, bytenr, blocksize,
					 0, root->root_key.objectid,
					 level, 0);
		if (wret)
			ret = wret;
	} else {
		struct btrfs_disk_key mid_key;
		btrfs_node_key(mid, &mid_key, 0);
		btrfs_set_node_key(parent, &mid_key, pslot);
		btrfs_mark_buffer_dirty(parent);
	}

	if (left) {
		if (btrfs_header_nritems(left) > (uint)orig_slot) {
			extent_buffer_get(left);
			path->nodes[level] = left;
			path->slots[level + 1] -= 1;
			path->slots[level] = orig_slot;
			if (mid)
				free_extent_buffer(mid);
		} else {
			orig_slot -= btrfs_header_nritems(left);
			path->slots[level] = orig_slot;
		}
	}
	check_block(root, path, level);
	if (orig_ptr !=
	    btrfs_node_blockptr(path->nodes[level], path->slots[level]))
		return -1;
enospc:
	if (right)
		free_extent_buffer(right);
	if (left)
		free_extent_buffer(left);
	return ret;
}

static int noinline push_nodes_for_insert(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path, int level)
{
	struct extent_buffer *right = NULL;
	struct extent_buffer *mid;
	struct extent_buffer *left = NULL;
	struct extent_buffer *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];

	if (level == 0)
		return 1;

	mid = path->nodes[level];
	WARN_ON(btrfs_header_generation(mid) != trans->transid);

	if (level < BTRFS_MAX_LEVEL - 1) {
		parent = path->nodes[level + 1];
		pslot = path->slots[level + 1];
	}

	if (!parent)
		return 1;

	left = read_node_slot(root, parent, pslot - 1);

	if (extent_buffer_uptodate(left)) {
		u32 left_nr;
		left_nr = btrfs_header_nritems(left);
		if (left_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, left, parent,
					      pslot - 1, &left);
			if (ret)
				wret = 1;
			else {
				wret = push_node_left(trans, root,
						      left, mid, 0);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;
			orig_slot += left_nr;
			btrfs_node_key(mid, &disk_key, 0);
			btrfs_set_node_key(parent, &disk_key, pslot);
			btrfs_mark_buffer_dirty(parent);
			if (btrfs_header_nritems(left) > (uint)orig_slot) {
				path->nodes[level] = left;
				path->slots[level + 1] -= 1;
				path->slots[level] = orig_slot;
				free_extent_buffer(mid);
			} else {
				orig_slot -=
					btrfs_header_nritems(left);
				path->slots[level] = orig_slot;
				free_extent_buffer(left);
			}
			return 0;
		}
		free_extent_buffer(left);
	}
	right= read_node_slot(root, parent, pslot + 1);

	if (extent_buffer_uptodate(right)) {
		u32 right_nr;
		right_nr = btrfs_header_nritems(right);
		if (right_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, right,
					      parent, pslot + 1,
					      &right);
			if (ret)
				wret = 1;
			else {
				wret = balance_node_right(trans, root,
							  right, mid);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_node_key(right, &disk_key, 0);
			btrfs_set_node_key(parent, &disk_key, pslot + 1);
			btrfs_mark_buffer_dirty(parent);

			if (btrfs_header_nritems(mid) <= (uint)orig_slot) {
				path->nodes[level] = right;
				path->slots[level + 1] += 1;
				path->slots[level] = orig_slot -
					btrfs_header_nritems(mid);
				free_extent_buffer(mid);
			} else {
				free_extent_buffer(right);
			}
			return 0;
		}
		free_extent_buffer(right);
	}
	return 1;
}

void reada_for_search(struct btrfs_root *root, struct btrfs_path *path,
			     int level, int slot, u64 objectid)
{
	struct extent_buffer *node;
	struct btrfs_disk_key disk_key;
	u32 nritems;
	u64 search;
	u64 lowest_read;
	u64 highest_read;
	u64 nread = 0;
	int direction = path->reada;
	struct extent_buffer *eb;
	u32 nr;
	u32 blocksize;
	u32 nscan = 0;

	if (level != 1)
		return;

	if (!path->nodes[level])
		return;

	node = path->nodes[level];
	search = btrfs_node_blockptr(node, slot);
	blocksize = root->nodesize;
	eb = btrfs_find_tree_block(root, search, blocksize);
	if (eb) {
		free_extent_buffer(eb);
		return;
	}

	highest_read = search;
	lowest_read = search;

	nritems = btrfs_header_nritems(node);
	nr = slot;
	while(1) {
		if (direction < 0) {
			if (nr == 0)
				break;
			nr--;
		} else if (direction > 0) {
			nr++;
			if (nr >= nritems)
				break;
		}
		if (path->reada < 0 && objectid) {
			btrfs_node_key(node, &disk_key, nr);
			if (btrfs_disk_key_objectid(&disk_key) != objectid)
				break;
		}
		search = btrfs_node_blockptr(node, nr);
		if ((search >= lowest_read && search <= highest_read) ||
		    (search < lowest_read && lowest_read - search <= 32768) ||
		    (search > highest_read && search - highest_read <= 32768)) {
			readahead_tree_block(root, search, blocksize,
				     btrfs_node_ptr_generation(node, nr));
			nread += blocksize;
		}
		nscan++;
		if (path->reada < 2 && (nread > (256 * 1024) || nscan > 32))
			break;
		if(nread > (1024 * 1024) || nscan > 128)
			break;

		if (search < lowest_read)
			lowest_read = search;
		if (search > highest_read)
			highest_read = search;
	}
}

int btrfs_search_slot(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_path *p, int
		      ins_len, int cow)
{
	struct extent_buffer *b;
	int slot;
	int ret;
	int level;
	int should_reada = p->reada;
	u8 lowest_level = 0;

	lowest_level = p->lowest_level;
	WARN_ON(lowest_level && ins_len > 0);
	WARN_ON(p->nodes[0] != NULL);
again:
	b = root->node;
	extent_buffer_get(b);
	while (b) {
		level = btrfs_header_level(b);
		if (cow) {
			int wret;
			wret = btrfs_cow_block(trans, root, b,
					       p->nodes[level + 1],
					       p->slots[level + 1],
					       &b);
			if (wret) {
				free_extent_buffer(b);
				return wret;
			}
		}
		if(!cow && ins_len)
        {
            return -1;
        }    
		if (level != btrfs_header_level(b))
			WARN_ON(1);
		level = btrfs_header_level(b);
		p->nodes[level] = b;
		ret = check_block(root, p, level);
		if (ret)
			return -1;
		ret = bin_search(b, key, level, &slot);
		if (level != 0) {
			if (ret && slot > 0)
				slot -= 1;
			p->slots[level] = slot;
			if ((p->search_for_split || ins_len > 0) &&
			    btrfs_header_nritems(b) >=
			    BTRFS_NODEPTRS_PER_BLOCK(root) - 3) {
				int sret = split_node(trans, root, p, level);
				if (sret > 0)
                {
                    return -1;
                }    
				if (sret)
					return sret;
				b = p->nodes[level];
				slot = p->slots[level];
			} else if (ins_len < 0) {
				int sret = balance_level(trans, root, p,
							 level);
				if (sret)
					return sret;
				b = p->nodes[level];
				if (!b) {
					btrfs_release_path(p);
					goto again;
				}
				slot = p->slots[level];
				if (btrfs_header_nritems(b) == 1)
                {
                    return -1;
                }    
			}
			if (level == lowest_level)
				break;

			if (should_reada)
				reada_for_search(root, p, level, slot,
						 key->objectid);

			b = read_node_slot(root, b, slot);
			if (!extent_buffer_uptodate(b))
				return -EIO;
		} else {
			p->slots[level] = slot;
			if (ins_len > 0 &&
			    ins_len > btrfs_leaf_free_space(root, b)) {
				int sret = split_leaf(trans, root, key,
						      p, ins_len, ret == 0);
				if (sret > 0)
                {
                    return -1;
                }    
				if (sret)
					return sret;
			}
			return ret;
		}
	}
	return 1;
}

void btrfs_fixup_low_keys(struct btrfs_root *root, struct btrfs_path *path,
			  struct btrfs_disk_key *key, int level)
{
	int i;
	struct extent_buffer *t;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		int tslot = path->slots[i];
		if (!path->nodes[i])
			break;
		t = path->nodes[i];
		btrfs_set_node_key(t, key, tslot);
		btrfs_mark_buffer_dirty(path->nodes[i]);
		if (tslot != 0)
			break;
	}
}

int btrfs_set_item_key_safe(struct btrfs_root *root, struct btrfs_path *path,
			    struct btrfs_key *new_key)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *eb;
	int slot;

	eb = path->nodes[0];
	slot = path->slots[0];
	if (slot > 0) {
		btrfs_item_key(eb, &disk_key, slot - 1);
		if (btrfs_comp_keys(&disk_key, new_key) >= 0)
			return -1;
	}
	if (slot <(int) btrfs_header_nritems(eb) - 1) {
		btrfs_item_key(eb, &disk_key, slot + 1);
		if (btrfs_comp_keys(&disk_key, new_key) <= 0)
			return -1;
	}

	btrfs_cpu_key_to_disk(&disk_key, new_key);
	btrfs_set_item_key(eb, &disk_key, slot);
	btrfs_mark_buffer_dirty(eb);
	if (slot == 0)
		btrfs_fixup_low_keys(root, path, &disk_key, 1);
	return 0;
}

static int push_node_left(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *dst,
			  struct extent_buffer *src, int empty)
{
	int push_items = 0;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;
	WARN_ON(btrfs_header_generation(src) != trans->transid);
	WARN_ON(btrfs_header_generation(dst) != trans->transid);

	if (!empty && src_nritems <= 8)
		return 1;

	if (push_items <= 0) {
		return 1;
	}

	if (empty) {
		push_items = min(src_nritems, push_items);
		if (push_items < src_nritems) {
			if (src_nritems - push_items < 8) {
				if (push_items <= 8)
					return 1;
				push_items -= 8;
			}
		}
	} else
		push_items = min(src_nritems - 8, push_items);

	copy_extent_buffer(dst, src,
			   btrfs_node_key_ptr_offset(dst_nritems),
			   btrfs_node_key_ptr_offset(0),
		           push_items * sizeof(struct btrfs_key_ptr));

	if (push_items < src_nritems) {
		memmove_extent_buffer(src, btrfs_node_key_ptr_offset(0),
				      btrfs_node_key_ptr_offset(push_items),
				      (src_nritems - push_items) *
				      sizeof(struct btrfs_key_ptr));
	}
	btrfs_set_header_nritems(src, src_nritems - push_items);
	btrfs_set_header_nritems(dst, dst_nritems + push_items);
	btrfs_mark_buffer_dirty(src);
	btrfs_mark_buffer_dirty(dst);

	return ret;
}

static int balance_node_right(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct extent_buffer *dst,
			      struct extent_buffer *src)
{
	int push_items = 0;
	int max_push;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	WARN_ON(btrfs_header_generation(src) != trans->transid);
	WARN_ON(btrfs_header_generation(dst) != trans->transid);

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;
	if (push_items <= 0) {
		return 1;
	}

	if (src_nritems < 4) {
		return 1;
	}

	max_push = src_nritems / 2 + 1;
	if (max_push >= src_nritems) {
		return 1;
	}

	if (max_push < push_items)
		push_items = max_push;

	memmove_extent_buffer(dst, btrfs_node_key_ptr_offset(push_items),
				      btrfs_node_key_ptr_offset(0),
				      (dst_nritems) *
				      sizeof(struct btrfs_key_ptr));

	copy_extent_buffer(dst, src,
			   btrfs_node_key_ptr_offset(0),
			   btrfs_node_key_ptr_offset(src_nritems - push_items),
		           push_items * sizeof(struct btrfs_key_ptr));

	btrfs_set_header_nritems(src, src_nritems - push_items);
	btrfs_set_header_nritems(dst, dst_nritems + push_items);

	btrfs_mark_buffer_dirty(src);
	btrfs_mark_buffer_dirty(dst);

	return ret;
}

static int noinline insert_new_root(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_path *path, int level)
{
	u64 lower_gen;
	struct extent_buffer *lower;
	struct extent_buffer *c;
	struct extent_buffer *old;
	struct btrfs_disk_key lower_key;

	if (path->nodes[level])
    {
        return -1;
    }    
	if (path->nodes[level-1] != root->node)
    {
        return -1;
    }   

	lower = path->nodes[level-1];
	if (level == 1)
		btrfs_item_key(lower, &lower_key, 0);
	else
		btrfs_node_key(lower, &lower_key, 0);

	c = btrfs_alloc_free_block(trans, root, root->nodesize,
				   root->root_key.objectid, &lower_key, 
				   level, root->node->start, 0);

	if (IS_ERR(c))
		return PTR_ERR(c);

	memset_extent_buffer(c, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_nritems(c, 1);
	btrfs_set_header_level(c, level);
	btrfs_set_header_bytenr(c, c->start);
	btrfs_set_header_generation(c, trans->transid);
	btrfs_set_header_backref_rev(c, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(c, root->root_key.objectid);

	write_extent_buffer(c, root->fs_info->fsid,
			    btrfs_header_fsid(), BTRFS_FSID_SIZE);

	write_extent_buffer(c, root->fs_info->chunk_tree_uuid,
			    btrfs_header_chunk_tree_uuid(c),
			    BTRFS_UUID_SIZE);

	btrfs_set_node_key(c, &lower_key, 0);
	btrfs_set_node_blockptr(c, 0, lower->start);
	lower_gen = btrfs_header_generation(lower);
	WARN_ON(lower_gen != trans->transid);

	btrfs_set_node_ptr_generation(c, 0, lower_gen);

	btrfs_mark_buffer_dirty(c);

	old = root->node;
	root->node = c;

	free_extent_buffer(old);

	add_root_to_dirty_list(root);
	extent_buffer_get(c);
	path->nodes[level] = c;
	path->slots[level] = 0;
	return 0;
}

static int insert_ptr(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, struct btrfs_disk_key
		      *key, u64 bytenr, int slot, int level)
{
	struct extent_buffer *lower;
	uint nritems;

	if (!path->nodes[level])
    {
        return -1;
    }    
	lower = path->nodes[level];
	nritems = btrfs_header_nritems(lower);
	if (slot > (int)nritems)
		return -1;
	if (nritems == BTRFS_NODEPTRS_PER_BLOCK(root))
		return -1;
	if (slot != (int)nritems) {
		memmove_extent_buffer(lower,
			      btrfs_node_key_ptr_offset(slot + 1),
			      btrfs_node_key_ptr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_key_ptr));
	}
	btrfs_set_node_key(lower, key, slot);
	btrfs_set_node_blockptr(lower, slot, bytenr);
	WARN_ON(trans->transid == 0);
	btrfs_set_node_ptr_generation(lower, slot, trans->transid);
	btrfs_set_header_nritems(lower, nritems + 1);
	btrfs_mark_buffer_dirty(lower);
	return 0;
}

static int split_node(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, int level)
{
	struct extent_buffer *c;
	struct extent_buffer *split;
	struct btrfs_disk_key disk_key;
	int mid;
	int ret;
	int wret;
	u32 c_nritems;

	c = path->nodes[level];
	WARN_ON(btrfs_header_generation(c) != trans->transid);
	if (c == root->node) {
		ret = insert_new_root(trans, root, path, level + 1);
		if (ret)
			return ret;
	} else {
		ret = push_nodes_for_insert(trans, root, path, level);
		c = path->nodes[level];
		if (!ret && btrfs_header_nritems(c) <
		    BTRFS_NODEPTRS_PER_BLOCK(root) - 3)
			return 0;
		if (ret < 0)
			return ret;
	}

	c_nritems = btrfs_header_nritems(c);
	mid = (c_nritems + 1) / 2;
	btrfs_node_key(c, &disk_key, mid);

	split = btrfs_alloc_free_block(trans, root, root->nodesize,
					root->root_key.objectid,
					&disk_key, level, c->start, 0);
	if (IS_ERR(split))
		return PTR_ERR(split);

	memset_extent_buffer(split, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_level(split, btrfs_header_level(c));
	btrfs_set_header_bytenr(split, split->start);
	btrfs_set_header_generation(split, trans->transid);
	btrfs_set_header_backref_rev(split, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(split, root->root_key.objectid);
	write_extent_buffer(split, root->fs_info->fsid,
			    btrfs_header_fsid(), BTRFS_FSID_SIZE);
	write_extent_buffer(split, root->fs_info->chunk_tree_uuid,
			    btrfs_header_chunk_tree_uuid(split),
			    BTRFS_UUID_SIZE);


	copy_extent_buffer(split, c,
			   btrfs_node_key_ptr_offset(0),
			   btrfs_node_key_ptr_offset(mid),
			   (c_nritems - mid) * sizeof(struct btrfs_key_ptr));
	btrfs_set_header_nritems(split, c_nritems - mid);
	btrfs_set_header_nritems(c, mid);
	ret = 0;

	btrfs_mark_buffer_dirty(c);
	btrfs_mark_buffer_dirty(split);

	wret = insert_ptr(trans, root, path, &disk_key, split->start,
			  path->slots[level + 1] + 1,
			  level + 1);
	if (wret)
		ret = wret;

	if (path->slots[level] >= mid) {
		path->slots[level] -= mid;
		free_extent_buffer(c);
		path->nodes[level] = split;
		path->slots[level + 1] += 1;
	} else {
		free_extent_buffer(split);
	}
	return ret;
}

static int leaf_space_used(struct extent_buffer *l, int start, int nr)
{
	int data_len;
	int nritems = btrfs_header_nritems(l);
	int end = min(nritems, start + nr) - 1;

	if (!nr)
		return 0;
	data_len = btrfs_item_end_nr(l, start);
	data_len = data_len - btrfs_item_offset_nr(l, end);
	data_len += sizeof(struct btrfs_item) * nr;
	WARN_ON(data_len < 0);
	return data_len;
}

int btrfs_leaf_free_space(struct btrfs_root *root, struct extent_buffer *leaf)
{
	u32 nodesize = (root ? BTRFS_LEAF_DATA_SIZE(root) : leaf->len);
	int nritems = btrfs_header_nritems(leaf);
	int ret;
	ret = nodesize - leaf_space_used(leaf, 0, nritems);
	return ret;
}

static int push_leaf_right(struct btrfs_trans_handle *trans, struct btrfs_root
			   *root, struct btrfs_path *path, int data_size,
			   int empty)
{
	struct extent_buffer *left = path->nodes[0];
	struct extent_buffer *right;
	struct extent_buffer *upper;
	struct btrfs_disk_key disk_key;
	int slot;
	u32 i;
	int free_space;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 left_nritems;
	u32 nr;
	u32 right_nritems;
	u32 data_end;
	u32 this_item_size;
	int ret;

	slot = path->slots[1];
	if (!path->nodes[1]) {
		return 1;
	}
	upper = path->nodes[1];
	if (slot >= (int)btrfs_header_nritems(upper) - 1)
		return 1;

	right = read_node_slot(root, upper, slot + 1);
	if (!extent_buffer_uptodate(right)) {
		if (IS_ERR(right))
			return PTR_ERR(right);
		return -EIO;
	}
	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size) {
		free_extent_buffer(right);
		return 1;
	}

	ret = btrfs_cow_block(trans, root, right, upper,
			      slot + 1, &right);
	if (ret) {
		free_extent_buffer(right);
		return 1;
	}
	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size) {
		free_extent_buffer(right);
		return 1;
	}

	left_nritems = btrfs_header_nritems(left);
	if (left_nritems == 0) {
		free_extent_buffer(right);
		return 1;
	}

	if (empty)
		nr = 0;
	else
		nr = 1;

	i = left_nritems - 1;
	while (i >= nr) {
		item = btrfs_item_nr(i);

		if (path->slots[0] == (int)i)
			push_space += data_size + sizeof(*item);

		this_item_size = btrfs_item_size(left, item);
		if (this_item_size + sizeof(*item) + push_space > (long unsigned int)free_space)
			break;
		push_items++;
		push_space += this_item_size + sizeof(*item);
		if (i == 0)
			break;
		i--;
	}

	if (push_items == 0) {
		free_extent_buffer(right);
		return 1;
	}

	if (!empty && push_items == (int)left_nritems)
		WARN_ON(1);

	right_nritems = btrfs_header_nritems(right);

	push_space = btrfs_item_end_nr(left, left_nritems - push_items);
	push_space -= leaf_data_end(root, left);

	data_end = leaf_data_end(root, right);
	memmove_extent_buffer(right,
			      btrfs_leaf_data(right) + data_end - push_space,
			      btrfs_leaf_data(right) + data_end,
			      BTRFS_LEAF_DATA_SIZE(root) - data_end);

	copy_extent_buffer(right, left, btrfs_leaf_data(right) +
		     BTRFS_LEAF_DATA_SIZE(root) - push_space,
		     btrfs_leaf_data(left) + leaf_data_end(root, left),
		     push_space);

	memmove_extent_buffer(right, btrfs_item_nr_offset(push_items),
			      btrfs_item_nr_offset(0),
			      right_nritems * sizeof(struct btrfs_item));

	copy_extent_buffer(right, left, btrfs_item_nr_offset(0),
		   btrfs_item_nr_offset(left_nritems - push_items),
		   push_items * sizeof(struct btrfs_item));
	right_nritems += push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(root);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(i);
		push_space -= btrfs_item_size(right, item);
		btrfs_set_item_offset(right, item, push_space);
	}

	left_nritems -= push_items;
	btrfs_set_header_nritems(left, left_nritems);

	if (left_nritems)
		btrfs_mark_buffer_dirty(left);
	btrfs_mark_buffer_dirty(right);

	btrfs_item_key(right, &disk_key, 0);
	btrfs_set_node_key(upper, &disk_key, slot + 1);
	btrfs_mark_buffer_dirty(upper);

	if (path->slots[0] >= (int)left_nritems) {
		path->slots[0] -= left_nritems;
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = right;
		path->slots[1] += 1;
	} else {
		free_extent_buffer(right);
	}
	return 0;
}

static int push_leaf_left(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, int data_size,
			  int empty)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *right = path->nodes[0];
	struct extent_buffer *left;
	int slot;
	uint i;
	int free_space;
	uint push_space = 0;
	uint push_items = 0;
	struct btrfs_item *item;
	u32 old_left_nritems;
	u32 right_nritems;
	u32 nr;
	int ret = 0;
	u32 this_item_size;
	u32 old_left_item_size;

	slot = path->slots[1];
	if (slot == 0)
		return 1;
	if (!path->nodes[1])
		return 1;

	right_nritems = btrfs_header_nritems(right);
	if (right_nritems == 0) {
		return 1;
	}

	left = read_node_slot(root, path->nodes[1], slot - 1);
	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size) {
		free_extent_buffer(left);
		return 1;
	}

	ret = btrfs_cow_block(trans, root, left,
			      path->nodes[1], slot - 1, &left);
	if (ret) {
		free_extent_buffer(left);
		return 1;
	}

	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size) {
		free_extent_buffer(left);
		return 1;
	}

	if (empty)
		nr = right_nritems;
	else
		nr = right_nritems - 1;

	for (i = 0; i < nr; i++) {
		item = btrfs_item_nr(i);

		if (path->slots[0] == (int)i)
			push_space += data_size + sizeof(*item);

		this_item_size = btrfs_item_size(right, item);
		if (this_item_size + sizeof(*item) + push_space > (long unsigned int)free_space)
			break;

		push_items++;
		push_space += this_item_size + sizeof(*item);
	}

	if (push_items == 0) {
		free_extent_buffer(left);
		return 1;
	}
	if (!empty && push_items == btrfs_header_nritems(right))
		WARN_ON(1);

	copy_extent_buffer(left, right,
			   btrfs_item_nr_offset(btrfs_header_nritems(left)),
			   btrfs_item_nr_offset(0),
			   push_items * sizeof(struct btrfs_item));

	push_space = BTRFS_LEAF_DATA_SIZE(root) -
		     btrfs_item_offset_nr(right, push_items -1);

	copy_extent_buffer(left, right, btrfs_leaf_data(left) +
		     leaf_data_end(root, left) - push_space,
		     btrfs_leaf_data(right) +
		     btrfs_item_offset_nr(right, push_items - 1),
		     push_space);
	old_left_nritems = btrfs_header_nritems(left);
	if (old_left_nritems == 0)
    {
        return -1;
    }    

	old_left_item_size = btrfs_item_offset_nr(left, old_left_nritems - 1);
	for (i = old_left_nritems; i < old_left_nritems + push_items; i++) {
		u32 ioff;

		item = btrfs_item_nr(i);
		ioff = btrfs_item_offset(left, item);
		btrfs_set_item_offset(left, item,
		      ioff - (BTRFS_LEAF_DATA_SIZE(root) - old_left_item_size));
	}
	btrfs_set_header_nritems(left, old_left_nritems + push_items);

	if (push_items > right_nritems) {
		WARN_ON(1);
	}

	if (push_items < right_nritems) {
		push_space = btrfs_item_offset_nr(right, push_items - 1) -
						  leaf_data_end(root, right);
		memmove_extent_buffer(right, btrfs_leaf_data(right) +
				      BTRFS_LEAF_DATA_SIZE(root) - push_space,
				      btrfs_leaf_data(right) +
				      leaf_data_end(root, right), push_space);

		memmove_extent_buffer(right, btrfs_item_nr_offset(0),
			      btrfs_item_nr_offset(push_items),
			     (btrfs_header_nritems(right) - push_items) *
			     sizeof(struct btrfs_item));
	}
	right_nritems -= push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(root);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(i);
		push_space = push_space - btrfs_item_size(right, item);
		btrfs_set_item_offset(right, item, push_space);
	}

	btrfs_mark_buffer_dirty(left);
	if (right_nritems)
		btrfs_mark_buffer_dirty(right);

	btrfs_item_key(right, &disk_key, 0);
	btrfs_fixup_low_keys(root, path, &disk_key, 1);

	if (path->slots[0] < (int)push_items) {
		path->slots[0] += old_left_nritems;
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = left;
		path->slots[1] -= 1;
	} else {
		free_extent_buffer(left);
		path->slots[0] -= push_items;
	}
	if (path->slots[0] < 0)
    {
        return -1;
    }    
	return ret;
}

static noinline int copy_for_split(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_path *path,
			       struct extent_buffer *l,
			       struct extent_buffer *right,
			       int slot, int mid, int nritems)
{
	int data_copy_size;
	int rt_data_off;
	int i;
	int ret = 0;
	int wret;
	struct btrfs_disk_key disk_key;

	nritems = nritems - mid;
	btrfs_set_header_nritems(right, nritems);
	data_copy_size = btrfs_item_end_nr(l, mid) - leaf_data_end(root, l);

	copy_extent_buffer(right, l, btrfs_item_nr_offset(0),
			   btrfs_item_nr_offset(mid),
			   nritems * sizeof(struct btrfs_item));

	copy_extent_buffer(right, l,
		     btrfs_leaf_data(right) + BTRFS_LEAF_DATA_SIZE(root) -
		     data_copy_size, btrfs_leaf_data(l) +
		     leaf_data_end(root, l), data_copy_size);

	rt_data_off = BTRFS_LEAF_DATA_SIZE(root) -
		      btrfs_item_end_nr(l, mid);

	for (i = 0; i < nritems; i++) {
		struct btrfs_item *item = btrfs_item_nr(i);
		u32 ioff = btrfs_item_offset(right, item);
		btrfs_set_item_offset(right, item, ioff + rt_data_off);
	}

	btrfs_set_header_nritems(l, mid);
	ret = 0;
	btrfs_item_key(right, &disk_key, 0);
	wret = insert_ptr(trans, root, path, &disk_key, right->start,
			  path->slots[1] + 1, 1);
	if (wret)
		ret = wret;

	btrfs_mark_buffer_dirty(right);
	btrfs_mark_buffer_dirty(l);
	if (path->slots[0] != slot)
    {
        return -1;
    }    

	if (mid <= slot) {
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = right;
		path->slots[0] -= mid;
		path->slots[1] += 1;
	} else {
		free_extent_buffer(right);
	}

	if (path->slots[0] < 0)
    {
        return -1;
    }    

	return ret;
}

static noinline int split_leaf(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_key *ins_key,
			       struct btrfs_path *path, int data_size,
			       int extend)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *l;
	u32 nritems;
	uint mid;
	uint slot;
	struct extent_buffer *right;
	int ret = 0;
	int wret;
	uint split;
	uint num_doubles = 0;

	l = path->nodes[0];
	slot = path->slots[0];
	if (extend && data_size + btrfs_item_size_nr(l, slot) +
	    sizeof(struct btrfs_item) > BTRFS_LEAF_DATA_SIZE(root))
		return -EOVERFLOW;

	if (data_size && ins_key->type != BTRFS_DIR_ITEM_KEY) {
		wret = push_leaf_right(trans, root, path, data_size, 0);
		if (wret < 0)
			return wret;
		if (wret) {
			wret = push_leaf_left(trans, root, path, data_size, 0);
			if (wret < 0)
				return wret;
		}
		l = path->nodes[0];

		if (btrfs_leaf_free_space(root, l) >= data_size)
			return 0;
	}

	if (!path->nodes[1]) {
		ret = insert_new_root(trans, root, path, 1);
		if (ret)
			return ret;
	}
again:
	split = 1;
	l = path->nodes[0];
	slot = path->slots[0];
	nritems = btrfs_header_nritems(l);
	mid = (nritems + 1) / 2;

	if (mid <= slot) {
		if (nritems == 1 ||
		    leaf_space_used(l, mid, nritems - mid) + data_size >
			(int)BTRFS_LEAF_DATA_SIZE(root)) {
			if (slot >= nritems) {
				split = 0;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    data_size > (int)BTRFS_LEAF_DATA_SIZE(root)) {
					split = 2;
				}
			}
		}
	} else {
		if (leaf_space_used(l, 0, mid) + data_size >
			(int)BTRFS_LEAF_DATA_SIZE(root)) {
			if (!extend && data_size && slot == 0) {
				split = 0;
			} else if ((extend || !data_size) && slot == 0) {
				mid = 1;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    data_size >(int) BTRFS_LEAF_DATA_SIZE(root)) {
					split = 2 ;
				}
			}
		}
	}
	
	if (split == 0)
		btrfs_cpu_key_to_disk(&disk_key, ins_key);
	else
		btrfs_item_key(l, &disk_key, mid);

	right = btrfs_alloc_free_block(trans, root, root->nodesize,
					root->root_key.objectid,
					&disk_key, 0, l->start, 0);
	if (IS_ERR(right)) {
		return PTR_ERR(right);
	}

	memset_extent_buffer(right, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_bytenr(right, right->start);
	btrfs_set_header_generation(right, trans->transid);
	btrfs_set_header_backref_rev(right, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(right, root->root_key.objectid);
	btrfs_set_header_level(right, 0);
	write_extent_buffer(right, root->fs_info->fsid,
			    btrfs_header_fsid(), BTRFS_FSID_SIZE);

	write_extent_buffer(right, root->fs_info->chunk_tree_uuid,
			    btrfs_header_chunk_tree_uuid(right),
			    BTRFS_UUID_SIZE);

	if (split == 0) {
		if (mid <= slot) {
			btrfs_set_header_nritems(right, 0);
			wret = insert_ptr(trans, root, path,
					  &disk_key, right->start,
					  path->slots[1] + 1, 1);
			if (wret)
				ret = wret;

			free_extent_buffer(path->nodes[0]);
			path->nodes[0] = right;
			path->slots[0] = 0;
			path->slots[1] += 1;
		} else {
			btrfs_set_header_nritems(right, 0);
			wret = insert_ptr(trans, root, path,
					  &disk_key,
					  right->start,
					  path->slots[1], 1);
			if (wret)
				ret = wret;
			free_extent_buffer(path->nodes[0]);
			path->nodes[0] = right;
			path->slots[0] = 0;
			if (path->slots[1] == 0) {
				btrfs_fixup_low_keys(root, path,
						     &disk_key, 1);
			}
		}
		btrfs_mark_buffer_dirty(right);
		return ret;
	}

	ret = copy_for_split(trans, root, path, l, right, slot, mid, nritems);
	if (ret)
    {
        return -1;
    }    

	if (split == 2) {
		if (num_doubles != 0)
        {
            return -1;
        }    
		num_doubles++;
		goto again;
	}

	return ret;
}

int btrfs_split_item(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root,
		     struct btrfs_path *path,
		     struct btrfs_key *new_key,
		     unsigned long split_offset)
{
	u32 item_size;
	struct extent_buffer *leaf;
	struct btrfs_key orig_key;
	struct btrfs_item *item;
	struct btrfs_item *new_item;
	int ret = 0;
	uint slot;
	u32 nritems;
	u32 orig_offset;
	struct btrfs_disk_key disk_key;
	char *buf;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &orig_key, path->slots[0]);
	if (btrfs_leaf_free_space(root, leaf) >= (int)sizeof(struct btrfs_item))
		goto split;

	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	btrfs_release_path(path);

	path->search_for_split = 1;

	ret = btrfs_search_slot(trans, root, &orig_key, path, 0, 1);
	path->search_for_split = 0;

	if (ret != 0 || item_size != btrfs_item_size_nr(path->nodes[0],
							path->slots[0])) {
		return -EAGAIN;
	}

	ret = split_leaf(trans, root, &orig_key, path, 0, 0);
	if (ret)
    {
        return -1;
    }    

	leaf = path->nodes[0];

split:
	item = btrfs_item_nr(path->slots[0]);
	orig_offset = btrfs_item_offset(leaf, item);
	item_size = btrfs_item_size(leaf, item);


	buf = kmalloc(item_size, GFP_NOFS);
	if (!buf)
    {
        return -1;
    }    
	read_extent_buffer(leaf, buf, btrfs_item_ptr_offset(leaf,
			    path->slots[0]), item_size);
	slot = path->slots[0] + 1;
	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);

	if (slot != nritems) {
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + 1),
			      btrfs_item_nr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_item));

	}

	btrfs_cpu_key_to_disk(&disk_key, new_key);
	btrfs_set_item_key(leaf, &disk_key, slot);

	new_item = btrfs_item_nr(slot);

	btrfs_set_item_offset(leaf, new_item, orig_offset);
	btrfs_set_item_size(leaf, new_item, item_size - split_offset);

	btrfs_set_item_offset(leaf, item,
			      orig_offset + item_size - split_offset);
	btrfs_set_item_size(leaf, item, split_offset);

	btrfs_set_header_nritems(leaf, nritems + 1);

	write_extent_buffer(leaf, buf,
			    btrfs_item_ptr_offset(leaf, path->slots[0]),
			    split_offset);

	write_extent_buffer(leaf, buf + split_offset,
			    btrfs_item_ptr_offset(leaf, slot),
			    item_size - split_offset);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
	if (btrfs_leaf_free_space(root, leaf) < 0) {
		return -1;
	}
	kfree(buf);
	return ret;
}

int btrfs_truncate_item(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct btrfs_path *path,
			u32 new_size, int from_end)
{
	int ret = 0;
	uint slot;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data_start;
	unsigned int old_size;
	unsigned int size_diff;
	uint i;

	leaf = path->nodes[0];
	slot = path->slots[0];

	old_size = btrfs_item_size_nr(leaf, slot);
	if (old_size == new_size)
		return 0;

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	old_data_start = btrfs_item_offset_nr(leaf, slot);

	size_diff = old_size - new_size;

	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(i);
		ioff = btrfs_item_offset(leaf, item);
		btrfs_set_item_offset(leaf, item, ioff + size_diff);
	}

	if (from_end) {
		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + size_diff, btrfs_leaf_data(leaf) +
			      data_end, old_data_start + new_size - data_end);
	} else {
		struct btrfs_disk_key disk_key;
		u64 offset;

		btrfs_item_key(leaf, &disk_key, slot);

		if (btrfs_disk_key_type(&disk_key) == BTRFS_EXTENT_DATA_KEY) {
			unsigned long ptr;
			struct btrfs_file_extent_item *fi;

			fi = btrfs_item_ptr(leaf, slot,
					    struct btrfs_file_extent_item);
			fi = (struct btrfs_file_extent_item *)(
			     (unsigned long)fi - size_diff);

			if (btrfs_file_extent_type(leaf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE) {
				ptr = btrfs_item_ptr_offset(leaf, slot);
				memmove_extent_buffer(leaf, ptr,
				        (unsigned long)fi,
				        offsetof(struct btrfs_file_extent_item,
						 disk_bytenr));
			}
		}

		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + size_diff, btrfs_leaf_data(leaf) +
			      data_end, old_data_start - data_end);

		offset = btrfs_disk_key_offset(&disk_key);
		btrfs_set_disk_key_offset(&disk_key, offset + size_diff);
		btrfs_set_item_key(leaf, &disk_key, slot);
		if (slot == 0)
			btrfs_fixup_low_keys(root, path, &disk_key, 1);
	}

	item = btrfs_item_nr(slot);
	btrfs_set_item_size(leaf, item, new_size);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
	if (btrfs_leaf_free_space(root, leaf) < 0) {
		return -1;
	}
	return ret;
}

int btrfs_extend_item(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root, struct btrfs_path *path,
		      u32 data_size)
{
	int ret = 0;
	uint slot;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data;
	unsigned int old_size;
	uint i;

	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < (int)data_size) {
		return -1;
	}
	slot = path->slots[0];
	old_data = btrfs_item_end_nr(leaf, slot);

	if (slot >= nritems) {
		return -1;
	}

	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(i);
		ioff = btrfs_item_offset(leaf, item);
		btrfs_set_item_offset(leaf, item, ioff - data_size);
	}

	memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
		      data_end - data_size, btrfs_leaf_data(leaf) +
		      data_end, old_data - data_end);

	data_end = old_data;
	old_size = btrfs_item_size_nr(leaf, slot);
	item = btrfs_item_nr(slot);
	btrfs_set_item_size(leaf, item, old_size + data_size);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
	if (btrfs_leaf_free_space(root, leaf) < 0) {
		return -1;
	}
	return ret;
}

int btrfs_insert_empty_items(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path,
			    struct btrfs_key *cpu_key, u32 *data_size,
			    int nr)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	int ret = 0;
	uint slot;
	uint i;
	u32 nritems;
	u32 total_size = 0;
	u32 total_data = 0;
	unsigned int data_end;
	struct btrfs_disk_key disk_key;

	for (i = 0; i < (uint)nr; i++) {
		total_data += data_size[i];
	}

	if (!root->node)
		return -1;

	total_size = total_data + nr * sizeof(struct btrfs_item);
	ret = btrfs_search_slot(trans, root, cpu_key, path, total_size, 1);
	if (ret == 0) {
		return -EEXIST;
	}
	if (ret < 0)
		goto out;

	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < (int)total_size) {
		return -1;
	}

	slot = path->slots[0];

	if (slot != nritems) {
		unsigned int old_data = btrfs_item_end_nr(leaf, slot);

		if (old_data < data_end) {
			return -1;
		}
		for (i = slot; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(i);
			ioff = btrfs_item_offset(leaf, item);
			btrfs_set_item_offset(leaf, item, ioff - total_data);
		}

		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + nr),
			      btrfs_item_nr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_item));

		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end - total_data, btrfs_leaf_data(leaf) +
			      data_end, old_data - data_end);
		data_end = old_data;
	}

	for (i = 0; i < (uint)nr; i++) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key + i);
		btrfs_set_item_key(leaf, &disk_key, slot + i);
		item = btrfs_item_nr(slot + i);
		btrfs_set_item_offset(leaf, item, data_end - data_size[i]);
		data_end -= data_size[i];
		btrfs_set_item_size(leaf, item, data_size[i]);
	}
	btrfs_set_header_nritems(leaf, nritems + nr);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
	if (slot == 0) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key);
		btrfs_fixup_low_keys(root, path, &disk_key, 1);
	}

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		return -1;
	}

out:
	return ret;
}

int btrfs_insert_item(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *cpu_key, void *data, u32
		      data_size)
{
	int ret = 0;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	unsigned long ptr;

	path = btrfs_alloc_path();
	if (!path)
    {
        return -1;
    }    
	ret = btrfs_insert_empty_item(trans, root, path, cpu_key, data_size);
	if (!ret) {
		leaf = path->nodes[0];
		ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
		write_extent_buffer(leaf, data, ptr, data_size);
		btrfs_mark_buffer_dirty(leaf);
	}
	btrfs_free_path(path);
	return ret;
}

int btrfs_del_ptr(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path, int level, int slot)
{
	struct extent_buffer *parent = path->nodes[level];
	u32 nritems;
	int ret = 0;

	nritems = btrfs_header_nritems(parent);
	if (slot != (int)nritems -1) {
		memmove_extent_buffer(parent,
			      btrfs_node_key_ptr_offset(slot),
			      btrfs_node_key_ptr_offset(slot + 1),
			      sizeof(struct btrfs_key_ptr) *
			      (nritems - slot - 1));
	}
	nritems--;
	btrfs_set_header_nritems(parent, nritems);
	if (nritems == 0 && parent == root->node) {
		if (btrfs_header_level(root->node) != 1)
        {
            return -1;
        }    
		btrfs_set_header_level(root->node, 0);
	} else if (slot == 0) {
		struct btrfs_disk_key disk_key;

		btrfs_node_key(parent, &disk_key, 0);
		btrfs_fixup_low_keys(root, path, &disk_key, level + 1);
	}
	btrfs_mark_buffer_dirty(parent);
	return ret;
}

static noinline int btrfs_del_leaf(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_path *path,
				   struct extent_buffer *leaf)
{
	int ret;

	WARN_ON(btrfs_header_generation(leaf) != trans->transid);
	ret = btrfs_del_ptr(trans, root, path, 1, path->slots[1]);
	if (ret)
		return ret;

	ret = btrfs_free_extent(trans, root, leaf->start, leaf->len,
				0, root->root_key.objectid, 0, 0);
	return ret;
}

int btrfs_del_items(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		    struct btrfs_path *path, int slot, int nr)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	int last_off;
	int dsize = 0;
	int ret = 0;
	int wret;
	uint i;
	u32 nritems;

	leaf = path->nodes[0];
	last_off = btrfs_item_offset_nr(leaf, slot + nr - 1);

	for (i = 0; i < (uint)nr; i++)
		dsize += btrfs_item_size_nr(leaf, slot + i);

	nritems = btrfs_header_nritems(leaf);

	if (slot + nr != (int)nritems) {
		int data_end = leaf_data_end(root, leaf);

		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + dsize,
			      btrfs_leaf_data(leaf) + data_end,
			      last_off - data_end);

		for (i = slot + nr; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(i);
			ioff = btrfs_item_offset(leaf, item);
			btrfs_set_item_offset(leaf, item, ioff + dsize);
		}

		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot),
			      btrfs_item_nr_offset(slot + nr),
			      sizeof(struct btrfs_item) *
			      (nritems - slot - nr));
	}
	btrfs_set_header_nritems(leaf, nritems - nr);
	nritems -= nr;

	if (nritems == 0) {
		if (leaf == root->node) {
			btrfs_set_header_level(leaf, 0);
		} else {
			clean_tree_block(trans, root, leaf);
			wait_on_tree_block_writeback(root, leaf);

			wret = btrfs_del_leaf(trans, root, path, leaf);
			if (ret)
            {
                return -1;
            }    
			if (wret)
				ret = wret;
		}
	} else {
		int used = leaf_space_used(leaf, 0, nritems);
		if (slot == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_item_key(leaf, &disk_key, 0);
			btrfs_fixup_low_keys(root, path, &disk_key, 1);
		}

		if (used <(int) BTRFS_LEAF_DATA_SIZE(root) / 4) {
			slot = path->slots[1];
			extent_buffer_get(leaf);

			wret = push_leaf_left(trans, root, path, 1, 1);
			if (wret < 0 && wret != -ENOSPC)
				ret = wret;

			if (path->nodes[0] == leaf &&
			    btrfs_header_nritems(leaf)) {
				wret = push_leaf_right(trans, root, path, 1, 1);
				if (wret < 0 && wret != -ENOSPC)
					ret = wret;
			}

			if (btrfs_header_nritems(leaf) == 0) {
				clean_tree_block(trans, root, leaf);
				wait_on_tree_block_writeback(root, leaf);

				path->slots[1] = slot;
				ret = btrfs_del_leaf(trans, root, path, leaf);
				if (ret)
                {
                    return -1;
                }    
				free_extent_buffer(leaf);

			} else {
				btrfs_mark_buffer_dirty(leaf);
				free_extent_buffer(leaf);
			}
		} else {
			btrfs_mark_buffer_dirty(leaf);
		}
	}
	return ret;
}

int btrfs_prev_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	int slot;
	int level = 1;
	struct extent_buffer *c;
	struct extent_buffer *next = NULL;

	while(level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level])
			return 1;

		slot = path->slots[level];
		c = path->nodes[level];
		if (slot == 0) {
			level++;
			if (level == BTRFS_MAX_LEVEL)
				return 1;
			continue;
		}
		slot--;

		next = read_node_slot(root, c, slot);
		if (!extent_buffer_uptodate(next)) {
			if (IS_ERR(next))
				return PTR_ERR(next);
			return -EIO;
		}
		break;
	}
	path->slots[level] = slot;
	while(1) {
		level--;
		c = path->nodes[level];
		free_extent_buffer(c);
		slot = btrfs_header_nritems(next);
		if (slot != 0)
			slot--;
		path->nodes[level] = next;
		path->slots[level] = slot;
		if (!level)
			break;
		next = read_node_slot(root, next, slot);
		if (!extent_buffer_uptodate(next)) {
			if (IS_ERR(next))
				return PTR_ERR(next);
			return -EIO;
		}
	}
	return 0;
}

int btrfs_next_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	uint slot;
	int level = 1;
	struct extent_buffer *c;
	struct extent_buffer *next = NULL;

	while(level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level])
			return 1;

		slot = path->slots[level] + 1;
		c = path->nodes[level];
		if (slot >= btrfs_header_nritems(c)) {
			level++;
			if (level == BTRFS_MAX_LEVEL)
				return 1;
			continue;
		}

		if (path->reada)
			reada_for_search(root, path, level, slot, 0);

		next = read_node_slot(root, c, slot);
		if (!extent_buffer_uptodate(next))
			return -EIO;
		break;
	}
	path->slots[level] = slot;
	while(1) {
		level--;
		c = path->nodes[level];
		free_extent_buffer(c);
		path->nodes[level] = next;
		path->slots[level] = 0;
		if (!level)
			break;
		if (path->reada)
			reada_for_search(root, path, level, 0, 0);
		next = read_node_slot(root, next, 0);
		if (!extent_buffer_uptodate(next))
			return -EIO;
	}
	return 0;
}

int btrfs_previous_item(struct btrfs_root *root,
			struct btrfs_path *path, u64 min_objectid,
			int type)
{
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	u32 nritems;
	int ret;

	while(1) {
		if (path->slots[0] == 0) {
			ret = btrfs_prev_leaf(root, path);
			if (ret != 0)
				return ret;
		} else {
			path->slots[0]--;
		}
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (nritems == 0)
			return 1;
		if (path->slots[0] == (int)nritems)
			path->slots[0]--;

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid < min_objectid)
			break;
		if (found_key.type == type)
			return 0;
		if (found_key.objectid == min_objectid &&
		    found_key.type < type)
			break;
	}
	return 1;
}

