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
#include <stdint.h>
#include <math.h>
#include <btrfs/crc32c.h>
#include <btrfs/radix-tree.h>
#include <btrfs/kerncompat.h>
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"
#include "volumes.h"
#include "free-space-cache.h"
#include "utils.h"

#define PENDING_EXTENT_INSERT 0
#define PENDING_EXTENT_DELETE 1
#define PENDING_BACKREF_UPDATE 2

struct pending_extent_op {
	int type;
	u64 bytenr;
	u64 num_bytes;
	u64 flags;
	struct btrfs_disk_key key;
	int level;
};

static int alloc_reserved_tree_block(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 root_objectid, u64 generation,
				     u64 flags, struct btrfs_disk_key *key,
				     int level, struct btrfs_key *ins);
                     
static int __free_extent(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 bytenr, u64 num_bytes, u64 parent,
			 u64 root_objectid, u64 owner_objectid,
			 u64 owner_offset, int refs_to_drop);

static int finish_current_insert(struct btrfs_trans_handle *trans, struct
				 btrfs_root *extent_root);
static int del_pending_extents(struct btrfs_trans_handle *trans, struct
			       btrfs_root *extent_root);

static struct btrfs_block_group_cache *
btrfs_find_block_group(struct btrfs_root *root, struct btrfs_block_group_cache
		       *hint, u64 search_start, int data, int owner);

static int remove_sb_from_cache(struct btrfs_root *root,
				struct btrfs_block_group_cache *cache)
{
	u64 bytenr;
	u64 *logical;
	int stripe_len;
	int i, nr, ret;
	struct extent_io_tree *free_space_cache;

	free_space_cache = &root->fs_info->free_space_cache;
	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		bytenr = btrfs_sb_offset(i);
		ret = btrfs_rmap_block(&root->fs_info->mapping_tree,
				       cache->key.objectid, bytenr, 0,
				       &logical, &nr, &stripe_len);
		BUG_ON(ret);
		while (nr--) {
			clear_extent_dirty(free_space_cache, logical[nr],
				logical[nr] + stripe_len - 1, GFP_NOFS);
		}
		kfree(logical);
	}
	return 0;
}

static int cache_block_group(struct btrfs_root *root,
			     struct btrfs_block_group_cache *block_group)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct extent_io_tree *free_space_cache;
	int slot;
	u64 last;
	u64 hole_size;

	if (!block_group)
		return 0;

	root = root->fs_info->extent_root;
	free_space_cache = &root->fs_info->free_space_cache;

	if (block_group->cached)
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 2;
	last = max_t(u64, block_group->key.objectid, BTRFS_SUPER_INFO_OFFSET);
	key.objectid = last;
	key.offset = 0;
	key.type = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;

	while(1) {
		leaf = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto err;
			if (ret == 0) {
				continue;
			} else {
				break;
			}
		}
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid < block_group->key.objectid) {
			goto next;
		}
		if (key.objectid >= block_group->key.objectid +
		    block_group->key.offset) {
			break;
		}

		if (key.type == BTRFS_EXTENT_ITEM_KEY ||
		    key.type == BTRFS_METADATA_ITEM_KEY) {
			if (key.objectid > last) {
				hole_size = key.objectid - last;
				set_extent_dirty(free_space_cache, last,
						 last + hole_size - 1,
						 GFP_NOFS);
			}
			if (key.type == BTRFS_METADATA_ITEM_KEY)
				last = key.objectid + root->nodesize;
			else
				last = key.objectid + key.offset;
		}
next:
		path->slots[0]++;
	}

	if (block_group->key.objectid +
	    block_group->key.offset > last) {
		hole_size = block_group->key.objectid +
			block_group->key.offset - last;
		set_extent_dirty(free_space_cache, last,
				 last + hole_size - 1, GFP_NOFS);
	}
	remove_sb_from_cache(root, block_group);
	block_group->cached = 1;
err:
	btrfs_free_path(path);
	return 0;
}

struct btrfs_block_group_cache *btrfs_lookup_first_block_group(struct
						       btrfs_fs_info *info,
						       u64 bytenr)
{
	struct extent_io_tree *block_group_cache;
	struct btrfs_block_group_cache *block_group = NULL;
	u64 ptr;
	u64 start;
	u64 end;
	int ret;

	bytenr = max_t(u64, bytenr,
		       BTRFS_SUPER_INFO_OFFSET + BTRFS_SUPER_INFO_SIZE);
	block_group_cache = &info->block_group_cache;
	ret = find_first_extent_bit(block_group_cache,
				    bytenr, &start, &end,
				    BLOCK_GROUP_DATA | BLOCK_GROUP_METADATA |
				    BLOCK_GROUP_SYSTEM);
	if (ret) {
		return NULL;
	}
	ret = get_state_private(block_group_cache, start, &ptr);
	if (ret)
		return NULL;

	block_group = (struct btrfs_block_group_cache *)(unsigned long)ptr;
	return block_group;
}

struct btrfs_block_group_cache *btrfs_lookup_block_group(struct
							 btrfs_fs_info *info,
							 u64 bytenr)
{
	struct extent_io_tree *block_group_cache;
	struct btrfs_block_group_cache *block_group = NULL;
	u64 ptr;
	u64 start;
	u64 end;
	int ret;

	block_group_cache = &info->block_group_cache;
	ret = find_first_extent_bit(block_group_cache,
				    bytenr, &start, &end,
				    BLOCK_GROUP_DATA | BLOCK_GROUP_METADATA |
				    BLOCK_GROUP_SYSTEM);
	if (ret) {
		return NULL;
	}
	ret = get_state_private(block_group_cache, start, &ptr);
	if (ret)
		return NULL;

	block_group = (struct btrfs_block_group_cache *)(unsigned long)ptr;
	if (block_group->key.objectid <= bytenr && bytenr <
	    block_group->key.objectid + block_group->key.offset)
		return block_group;
	return NULL;
}

static int block_group_bits(struct btrfs_block_group_cache *cache, u64 bits)
{
	return (cache->flags & bits) == bits;
}

static int noinline find_search_start(struct btrfs_root *root,
			      struct btrfs_block_group_cache **cache_ret,
			      u64 *start_ret, int num, int data)
{
	int ret;
	struct btrfs_block_group_cache *cache = *cache_ret;
	u64 last = *start_ret;
	u64 start = 0;
	u64 end = 0;
	u64 search_start = *start_ret;
	int wrapped = 0;

	if (!cache)
		goto out;
again:
	ret = cache_block_group(root, cache);
	if (ret)
		goto out;

	last = max(search_start, cache->key.objectid);
	if (cache->ro || !block_group_bits(cache, data))
		goto new_group;

	while(1) {
		ret = find_first_extent_bit(&root->fs_info->free_space_cache,
					    last, &start, &end, EXTENT_DIRTY);
		if (ret) {
			goto new_group;
		}

		start = max(last, start);
		last = end + 1;
		if (last - start < num) {
			continue;
		}
		if (start + num > cache->key.objectid + cache->key.offset) {
			goto new_group;
		}
		*start_ret = start;
		return 0;
	}
out:
	*start_ret = last;
	cache = btrfs_lookup_block_group(root->fs_info, search_start);
	if (!cache) {
		printk("Unable to find block group for %llu\n",
			(unsigned long long)search_start);
		WARN_ON(1);
	}
	return -ENOSPC;

new_group:
	last = cache->key.objectid + cache->key.offset;
wrapped:
	cache = btrfs_lookup_first_block_group(root->fs_info, last);
	if (!cache) {
		if (!wrapped) {
			wrapped = 1;
			last = search_start;
			goto wrapped;
		}
		goto out;
	}
	*cache_ret = cache;
	goto again;
}

static int block_group_state_bits(u64 flags)
{
	int bits = 0;
	if (flags & BTRFS_BLOCK_GROUP_DATA)
		bits |= BLOCK_GROUP_DATA;
	if (flags & BTRFS_BLOCK_GROUP_METADATA)
		bits |= BLOCK_GROUP_METADATA;
	if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
		bits |= BLOCK_GROUP_SYSTEM;
	return bits;
}

static struct btrfs_block_group_cache *
btrfs_find_block_group(struct btrfs_root *root, struct btrfs_block_group_cache
		       *hint, u64 search_start, int data, int owner)
{
	struct btrfs_block_group_cache *cache;
	struct extent_io_tree *block_group_cache;
	struct btrfs_block_group_cache *found_group = NULL;
	struct btrfs_fs_info *info = root->fs_info;
	u64 used;
	u64 last = 0;
	u64 hint_last;
	u64 start;
	u64 end;
	u64 free_check;
	u64 ptr;
	int bit;
	int ret;
	int full_search = 0;
	int factor = 10;

	block_group_cache = &info->block_group_cache;

	if (!owner)
		factor = 10;

	bit = block_group_state_bits(data);

	if (search_start) {
		struct btrfs_block_group_cache *shint;
		shint = btrfs_lookup_block_group(info, search_start);
		if (shint && !shint->ro && block_group_bits(shint, data)) {
			used = btrfs_block_group_used(&shint->item);
			if (used + shint->pinned <
			    div_factor(shint->key.offset, factor)) {
				return shint;
			}
		}
	}
	if (hint && !hint->ro && block_group_bits(hint, data)) {
		used = btrfs_block_group_used(&hint->item);
		if (used + hint->pinned <
		    div_factor(hint->key.offset, factor)) {
			return hint;
		}
		last = hint->key.objectid + hint->key.offset;
		hint_last = last;
	} else {
		if (hint)
			hint_last = max(hint->key.objectid, search_start);
		else
			hint_last = search_start;

		last = hint_last;
	}
again:
	while(1) {
		ret = find_first_extent_bit(block_group_cache, last,
					    &start, &end, bit);
		if (ret)
			break;

		ret = get_state_private(block_group_cache, start, &ptr);
		if (ret)
			break;

		cache = (struct btrfs_block_group_cache *)(unsigned long)ptr;
		last = cache->key.objectid + cache->key.offset;
		used = btrfs_block_group_used(&cache->item);

		if (!cache->ro && block_group_bits(cache, data)) {
			if (full_search)
				free_check = cache->key.offset;
			else
				free_check = div_factor(cache->key.offset,
							factor);

			if (used + cache->pinned < free_check) {
				found_group = cache;
				goto found;
			}
		}
		cond_resched();
	}
	if (!full_search) {
		last = search_start;
		full_search = 1;
		goto again;
	}
found:
	return found_group;
}


#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
static int convert_extent_item_v0(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct btrfs_path *path,
				  u64 owner, u32 extra_size)
{
	struct btrfs_extent_item *item;
	struct btrfs_extent_item_v0 *ei0;
	struct btrfs_extent_ref_v0 *ref0;
	struct btrfs_tree_block_info *bi;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u32 new_size = sizeof(*item);
	u64 refs;
	int ret;

	leaf = path->nodes[0];
	BUG_ON(btrfs_item_size_nr(leaf, path->slots[0]) != sizeof(*ei0));

	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	ei0 = btrfs_item_ptr(leaf, path->slots[0],
			     struct btrfs_extent_item_v0);
	refs = btrfs_extent_refs_v0(leaf, ei0);

	if (owner == (u64)-1) {
		while (1) {
			if (path->slots[0] >= btrfs_header_nritems(leaf)) {
				ret = btrfs_next_leaf(root, path);
				if (ret < 0)
					return ret;
				BUG_ON(ret > 0);
				leaf = path->nodes[0];
			}
			btrfs_item_key_to_cpu(leaf, &found_key,
					      path->slots[0]);
			BUG_ON(key.objectid != found_key.objectid);
			if (found_key.type != BTRFS_EXTENT_REF_V0_KEY) {
				path->slots[0]++;
				continue;
			}
			ref0 = btrfs_item_ptr(leaf, path->slots[0],
					      struct btrfs_extent_ref_v0);
			owner = btrfs_ref_objectid_v0(leaf, ref0);
			break;
		}
	}
	btrfs_release_path(path);

	if (owner < BTRFS_FIRST_FREE_OBJECTID)
		new_size += sizeof(*bi);

	new_size -= sizeof(*ei0);
	ret = btrfs_search_slot(trans, root, &key, path, new_size, 1);
	if (ret < 0)
		return ret;
	BUG_ON(ret);

	ret = btrfs_extend_item(trans, root, path, new_size);
	BUG_ON(ret);

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, item, refs);
	btrfs_set_extent_generation(leaf, item, 0);
	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		btrfs_set_extent_flags(leaf, item,
				       BTRFS_EXTENT_FLAG_TREE_BLOCK |
				       BTRFS_BLOCK_FLAG_FULL_BACKREF);
		bi = (struct btrfs_tree_block_info *)(item + 1);
		memset_extent_buffer(leaf, 0, (unsigned long)bi, sizeof(*bi));
		btrfs_set_tree_block_level(leaf, bi, (int)owner);
	} else {
		btrfs_set_extent_flags(leaf, item, BTRFS_EXTENT_FLAG_DATA);
	}
	btrfs_mark_buffer_dirty(leaf);
	return 0;
}
#endif

static u64 hash_extent_data_ref(u64 root_objectid, u64 owner, u64 offset)
{
	u32 high_crc = ~(u32)0;
	u32 low_crc = ~(u32)0;
	__le64 lenum;

	lenum = cpu_to_le64(root_objectid);
	high_crc = btrfs_crc32c(high_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(owner);
	low_crc = btrfs_crc32c(low_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(offset);
	low_crc = btrfs_crc32c(low_crc, &lenum, sizeof(lenum));

	return ((u64)high_crc << 31) ^ (u64)low_crc;
}

static u64 hash_extent_data_ref_item(struct extent_buffer *leaf,
				     struct btrfs_extent_data_ref *ref)
{
	return hash_extent_data_ref(btrfs_extent_data_ref_root(leaf, ref),
				    btrfs_extent_data_ref_objectid(leaf, ref),
				    btrfs_extent_data_ref_offset(leaf, ref));
}

static int match_extent_data_ref(struct extent_buffer *leaf,
				 struct btrfs_extent_data_ref *ref,
				 u64 root_objectid, u64 owner, u64 offset)
{
	if (btrfs_extent_data_ref_root(leaf, ref) != root_objectid ||
	    btrfs_extent_data_ref_objectid(leaf, ref) != owner ||
	    btrfs_extent_data_ref_offset(leaf, ref) != offset)
		return 0;
	return 1;
}

static noinline int lookup_extent_data_ref(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct btrfs_path *path,
					   u64 bytenr, u64 parent,
					   u64 root_objectid,
					   u64 owner, u64 offset)
{
	struct btrfs_key key;
	struct btrfs_extent_data_ref *ref;
	struct extent_buffer *leaf;
	u32 nritems;
	int ret;
	int recow;
	int err = -ENOENT;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_DATA_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_EXTENT_DATA_REF_KEY;
		key.offset = hash_extent_data_ref(root_objectid,
						  owner, offset);
	}
again:
	recow = 0;
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0) {
		err = ret;
		goto fail;
	}

	if (parent) {
		if (!ret)
			return 0;
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		key.type = BTRFS_EXTENT_REF_V0_KEY;
		btrfs_release_path(path);
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0) {
			err = ret;
			goto fail;
		}
		if (!ret)
			return 0;
#endif
		goto fail;
	}

	leaf = path->nodes[0];
	nritems = btrfs_header_nritems(leaf);
	while (1) {
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				err = ret;
			if (ret)
				goto fail;

			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
			recow = 1;
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid != bytenr ||
		    key.type != BTRFS_EXTENT_DATA_REF_KEY)
			goto fail;
		
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_data_ref);

		if (match_extent_data_ref(leaf, ref, root_objectid,
					  owner, offset)) {
			if (recow) {
				btrfs_release_path(path);
				goto again;
			}
			err = 0;
			break;
		}
		path->slots[0]++;
	}
fail:
	return err;
}

static noinline int insert_extent_data_ref(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct btrfs_path *path,
					   u64 bytenr, u64 parent,
					   u64 root_objectid, u64 owner,
					   u64 offset, int refs_to_add)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	u32 size;
	u32 num_refs;
	int ret;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_DATA_REF_KEY;
		key.offset = parent;
		size = sizeof(struct btrfs_shared_data_ref);
	} else {
		key.type = BTRFS_EXTENT_DATA_REF_KEY;
		key.offset = hash_extent_data_ref(root_objectid,
						  owner, offset);
		size = sizeof(struct btrfs_extent_data_ref);
	}

	ret = btrfs_insert_empty_item(trans, root, path, &key, size);
	if (ret && ret != -EEXIST)
		goto fail;

	leaf = path->nodes[0];
	if (parent) {
		struct btrfs_shared_data_ref *ref;
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_shared_data_ref);
		if (ret == 0) {
			btrfs_set_shared_data_ref_count(leaf, ref, refs_to_add);
		} else {
			num_refs = btrfs_shared_data_ref_count(leaf, ref);
			num_refs += refs_to_add;
			btrfs_set_shared_data_ref_count(leaf, ref, num_refs);
		}
	} else {
		struct btrfs_extent_data_ref *ref;
		while (ret == -EEXIST) {
			ref = btrfs_item_ptr(leaf, path->slots[0],
					     struct btrfs_extent_data_ref);
			if (match_extent_data_ref(leaf, ref, root_objectid,
						  owner, offset))
				break;
			btrfs_release_path(path);

			key.offset++;
			ret = btrfs_insert_empty_item(trans, root, path, &key,
						      size);
			if (ret && ret != -EEXIST)
				goto fail;

			leaf = path->nodes[0];
		}
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_data_ref);
		if (ret == 0) {
			btrfs_set_extent_data_ref_root(leaf, ref,
						       root_objectid);
			btrfs_set_extent_data_ref_objectid(leaf, ref, owner);
			btrfs_set_extent_data_ref_offset(leaf, ref, offset);
			btrfs_set_extent_data_ref_count(leaf, ref, refs_to_add);
		} else {
			num_refs = btrfs_extent_data_ref_count(leaf, ref);
			num_refs += refs_to_add;
			btrfs_set_extent_data_ref_count(leaf, ref, num_refs);
		}
	}
	btrfs_mark_buffer_dirty(leaf);
	ret = 0;
fail:
	btrfs_release_path(path);
	return ret;
}

static noinline int remove_extent_data_ref(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct btrfs_path *path,
					   int refs_to_drop)
{
	struct btrfs_key key;
	struct btrfs_extent_data_ref *ref1 = NULL;
	struct btrfs_shared_data_ref *ref2 = NULL;
	struct extent_buffer *leaf;
	u32 num_refs = 0;
	int ret = 0;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

	if (key.type == BTRFS_EXTENT_DATA_REF_KEY) {
		ref1 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_data_ref);
		num_refs = btrfs_extent_data_ref_count(leaf, ref1);
	} else if (key.type == BTRFS_SHARED_DATA_REF_KEY) {
		ref2 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_shared_data_ref);
		num_refs = btrfs_shared_data_ref_count(leaf, ref2);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	} else if (key.type == BTRFS_EXTENT_REF_V0_KEY) {
		struct btrfs_extent_ref_v0 *ref0;
		ref0 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_ref_v0);
		num_refs = btrfs_ref_count_v0(leaf, ref0);
#endif
	} else {
		BUG();
	}

	BUG_ON(num_refs < refs_to_drop);
	num_refs -= refs_to_drop;

	if (num_refs == 0) {
		ret = btrfs_del_item(trans, root, path);
	} else {
		if (key.type == BTRFS_EXTENT_DATA_REF_KEY)
			btrfs_set_extent_data_ref_count(leaf, ref1, num_refs);
		else if (key.type == BTRFS_SHARED_DATA_REF_KEY)
			btrfs_set_shared_data_ref_count(leaf, ref2, num_refs);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		else {
			struct btrfs_extent_ref_v0 *ref0;
			ref0 = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_extent_ref_v0);
			btrfs_set_ref_count_v0(leaf, ref0, num_refs);
		}
#endif
		btrfs_mark_buffer_dirty(leaf);
	}
	return ret;
}

static noinline u32 extent_data_ref_count(struct btrfs_root *root,
					  struct btrfs_path *path,
					  struct btrfs_extent_inline_ref *iref)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_extent_data_ref *ref1;
	struct btrfs_shared_data_ref *ref2;
	u32 num_refs = 0;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (iref) {
		if (btrfs_extent_inline_ref_type(leaf, iref) ==
		    BTRFS_EXTENT_DATA_REF_KEY) {
			ref1 = (struct btrfs_extent_data_ref *)(&iref->offset);
			num_refs = btrfs_extent_data_ref_count(leaf, ref1);
		} else {
			ref2 = (struct btrfs_shared_data_ref *)(iref + 1);
			num_refs = btrfs_shared_data_ref_count(leaf, ref2);
		}
	} else if (key.type == BTRFS_EXTENT_DATA_REF_KEY) {
		ref1 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_data_ref);
		num_refs = btrfs_extent_data_ref_count(leaf, ref1);
	} else if (key.type == BTRFS_SHARED_DATA_REF_KEY) {
		ref2 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_shared_data_ref);
		num_refs = btrfs_shared_data_ref_count(leaf, ref2);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	} else if (key.type == BTRFS_EXTENT_REF_V0_KEY) {
		struct btrfs_extent_ref_v0 *ref0;
		ref0 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_ref_v0);
		num_refs = btrfs_ref_count_v0(leaf, ref0);
#endif
	} else {
		BUG();
	}
	return num_refs;
}

static noinline int lookup_tree_block_ref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 bytenr, u64 parent,
					  u64 root_objectid)
{
	struct btrfs_key key;
	int ret;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_BLOCK_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_TREE_BLOCK_REF_KEY;
		key.offset = root_objectid;
	}

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0)
		ret = -ENOENT;
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (ret == -ENOENT && parent) {
		btrfs_release_path(path);
		key.type = BTRFS_EXTENT_REF_V0_KEY;
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0)
			ret = -ENOENT;
	}
#endif
	return ret;
}

static noinline int insert_tree_block_ref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 bytenr, u64 parent,
					  u64 root_objectid)
{
	struct btrfs_key key;
	int ret;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_BLOCK_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_TREE_BLOCK_REF_KEY;
		key.offset = root_objectid;
	}

	ret = btrfs_insert_empty_item(trans, root, path, &key, 0);

	btrfs_release_path(path);
	return ret;
}

static inline int extent_ref_type(u64 parent, u64 owner)
{
	int type;
	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		if (parent > 0)
			type = BTRFS_SHARED_BLOCK_REF_KEY;
		else
			type = BTRFS_TREE_BLOCK_REF_KEY;
	} else {
		if (parent > 0)
			type = BTRFS_SHARED_DATA_REF_KEY;
		else
			type = BTRFS_EXTENT_DATA_REF_KEY;
	}
	return type;
}

static int lookup_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref **ref_ret,
				 u64 bytenr, u64 num_bytes,
				 u64 parent, u64 root_objectid,
				 u64 owner, u64 offset, int insert)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_extent_inline_ref *iref;
	u64 flags;
	u32 item_size;
	unsigned long ptr;
	unsigned long end;
	int extra_size;
	int type;
	int want;
	int ret;
	int err = 0;
	int skinny_metadata =
		btrfs_fs_incompat(root->fs_info,
				  BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA);

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	want = extent_ref_type(parent, owner);
	if (insert)
		extra_size = btrfs_extent_inline_ref_size(want);
	else
		extra_size = -1;

	if (owner < BTRFS_FIRST_FREE_OBJECTID && skinny_metadata) {
		skinny_metadata = 1;
		key.type = BTRFS_METADATA_ITEM_KEY;
		key.offset = owner;
	} else if (skinny_metadata) {
		skinny_metadata = 0;
	}

again:
	ret = btrfs_search_slot(trans, root, &key, path, extra_size, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	if (ret > 0 && skinny_metadata) {
		skinny_metadata = 0;
		if (path->slots[0]) {
			path->slots[0]--;
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0]);
			if (key.objectid == bytenr &&
			    key.type == BTRFS_EXTENT_ITEM_KEY &&
			    key.offset == num_bytes)
				ret = 0;
		}
		if (ret) {
			key.type = BTRFS_EXTENT_ITEM_KEY;
			key.offset = num_bytes;
			btrfs_release_path(path);
			goto again;
		}
	}

	if (ret) {
		printf("Failed to find [%llu, %u, %llu]\n", key.objectid, key.type, key.offset);
		return -ENOENT;
	}

	BUG_ON(ret);

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		if (!insert) {
			err = -ENOENT;
			goto out;
		}
		ret = convert_extent_item_v0(trans, root, path, owner,
					     extra_size);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	}
#endif
	if (item_size < sizeof(*ei)) {
		printf("Size is %u, needs to be %u, slot %d\n",
		       (unsigned)item_size,
		       (unsigned)sizeof(*ei), path->slots[0]);
		btrfs_print_leaf(root, leaf);
		return -EINVAL;
	}
	BUG_ON(item_size < sizeof(*ei));

	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	flags = btrfs_extent_flags(leaf, ei);

	ptr = (unsigned long)(ei + 1);
	end = (unsigned long)ei + item_size;

	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK && !skinny_metadata) {
		ptr += sizeof(struct btrfs_tree_block_info);
		BUG_ON(ptr > end);
	} else if (!(flags & BTRFS_EXTENT_FLAG_TREE_BLOCK)) {
		if (!(flags & BTRFS_EXTENT_FLAG_DATA)) {
			return -EIO;
		}
	}

	err = -ENOENT;
	while (1) {
		if (ptr >= end) {
			WARN_ON(ptr > end);
			break;
		}
		iref = (struct btrfs_extent_inline_ref *)ptr;
		type = btrfs_extent_inline_ref_type(leaf, iref);
		if (want < type)
			break;
		if (want > type) {
			ptr += btrfs_extent_inline_ref_size(type);
			continue;
		}

		if (type == BTRFS_EXTENT_DATA_REF_KEY) {
			struct btrfs_extent_data_ref *dref;
			dref = (struct btrfs_extent_data_ref *)(&iref->offset);
			if (match_extent_data_ref(leaf, dref, root_objectid,
						  owner, offset)) {
				err = 0;
				break;
			}
			if (hash_extent_data_ref_item(leaf, dref) <
			    hash_extent_data_ref(root_objectid, owner, offset))
				break;
		} else {
			u64 ref_offset;
			ref_offset = btrfs_extent_inline_ref_offset(leaf, iref);
			if (parent > 0) {
				if (parent == ref_offset) {
					err = 0;
					break;
				}
				if (ref_offset < parent)
					break;
			} else {
				if (root_objectid == ref_offset) {
					err = 0;
					break;
				}
				if (ref_offset < root_objectid)
					break;
			}
		}
		ptr += btrfs_extent_inline_ref_size(type);
	}
	if (err == -ENOENT && insert) {
		if (item_size + extra_size >=
		    BTRFS_MAX_EXTENT_ITEM_SIZE(root)) {
			err = -EAGAIN;
			goto out;
		}
		if (find_next_key(path, &key) == 0 && key.objectid == bytenr &&
		    key.type < BTRFS_BLOCK_GROUP_ITEM_KEY) {
			err = -EAGAIN;
			goto out;
		}
	}
	*ref_ret = (struct btrfs_extent_inline_ref *)ptr;
out:
	return err;
}

static int setup_inline_extent_backref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_path *path,
				struct btrfs_extent_inline_ref *iref,
				u64 parent, u64 root_objectid,
				u64 owner, u64 offset, int refs_to_add)
{
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	unsigned long ptr;
	unsigned long end;
	unsigned long item_offset;
	u64 refs;
	int size;
	int type;
	int ret;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	item_offset = (unsigned long)iref - (unsigned long)ei;

	type = extent_ref_type(parent, owner);
	size = btrfs_extent_inline_ref_size(type);

	ret = btrfs_extend_item(trans, root, path, size);
	BUG_ON(ret);

	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, ei);
	refs += refs_to_add;
	btrfs_set_extent_refs(leaf, ei, refs);

	ptr = (unsigned long)ei + item_offset;
	end = (unsigned long)ei + btrfs_item_size_nr(leaf, path->slots[0]);
	if (ptr < end - size)
		memmove_extent_buffer(leaf, ptr + size, ptr,
				      end - size - ptr);

	iref = (struct btrfs_extent_inline_ref *)ptr;
	btrfs_set_extent_inline_ref_type(leaf, iref, type);
	if (type == BTRFS_EXTENT_DATA_REF_KEY) {
		struct btrfs_extent_data_ref *dref;
		dref = (struct btrfs_extent_data_ref *)(&iref->offset);
		btrfs_set_extent_data_ref_root(leaf, dref, root_objectid);
		btrfs_set_extent_data_ref_objectid(leaf, dref, owner);
		btrfs_set_extent_data_ref_offset(leaf, dref, offset);
		btrfs_set_extent_data_ref_count(leaf, dref, refs_to_add);
	} else if (type == BTRFS_SHARED_DATA_REF_KEY) {
		struct btrfs_shared_data_ref *sref;
		sref = (struct btrfs_shared_data_ref *)(iref + 1);
		btrfs_set_shared_data_ref_count(leaf, sref, refs_to_add);
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
	} else if (type == BTRFS_SHARED_BLOCK_REF_KEY) {
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
	} else {
		btrfs_set_extent_inline_ref_offset(leaf, iref, root_objectid);
	}
	btrfs_mark_buffer_dirty(leaf);
	return 0;
}

static int lookup_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref **ref_ret,
				 u64 bytenr, u64 num_bytes, u64 parent,
				 u64 root_objectid, u64 owner, u64 offset)
{
	int ret;

	ret = lookup_inline_extent_backref(trans, root, path, ref_ret,
					   bytenr, num_bytes, parent,
					   root_objectid, owner, offset, 0);
	if (ret != -ENOENT)
		return ret;

	btrfs_release_path(path);
	*ref_ret = NULL;

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = lookup_tree_block_ref(trans, root, path, bytenr, parent,
					    root_objectid);
	} else {
		ret = lookup_extent_data_ref(trans, root, path, bytenr, parent,
					     root_objectid, owner, offset);
	}
	return ret;
}

static int update_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref *iref,
				 int refs_to_mod)
{
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_extent_data_ref *dref = NULL;
	struct btrfs_shared_data_ref *sref = NULL;
	unsigned long ptr;
	unsigned long end;
	u32 item_size;
	int size;
	int type;
	int ret;
	u64 refs;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, ei);
	WARN_ON(refs_to_mod < 0 && refs + refs_to_mod <= 0);
	refs += refs_to_mod;
	btrfs_set_extent_refs(leaf, ei, refs);

	type = btrfs_extent_inline_ref_type(leaf, iref);

	if (type == BTRFS_EXTENT_DATA_REF_KEY) {
		dref = (struct btrfs_extent_data_ref *)(&iref->offset);
		refs = btrfs_extent_data_ref_count(leaf, dref);
	} else if (type == BTRFS_SHARED_DATA_REF_KEY) {
		sref = (struct btrfs_shared_data_ref *)(iref + 1);
		refs = btrfs_shared_data_ref_count(leaf, sref);
	} else {
		refs = 1;
		BUG_ON(refs_to_mod != -1);
	}

	BUG_ON(refs_to_mod < 0 && refs < -refs_to_mod);
	refs += refs_to_mod;

	if (refs > 0) {
		if (type == BTRFS_EXTENT_DATA_REF_KEY)
			btrfs_set_extent_data_ref_count(leaf, dref, refs);
		else
			btrfs_set_shared_data_ref_count(leaf, sref, refs);
	} else {
		size =  btrfs_extent_inline_ref_size(type);
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		ptr = (unsigned long)iref;
		end = (unsigned long)ei + item_size;
		if (ptr + size < end)
			memmove_extent_buffer(leaf, ptr, ptr + size,
					      end - ptr - size);
		item_size -= size;
		ret = btrfs_truncate_item(trans, root, path, item_size, 1);
		BUG_ON(ret);
	}
	btrfs_mark_buffer_dirty(leaf);
	return 0;
}

static int insert_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 u64 bytenr, u64 num_bytes, u64 parent,
				 u64 root_objectid, u64 owner,
				 u64 offset, int refs_to_add)
{
	struct btrfs_extent_inline_ref *iref;
	int ret;

	ret = lookup_inline_extent_backref(trans, root, path, &iref,
					   bytenr, num_bytes, parent,
					   root_objectid, owner, offset, 1);
	if (ret == 0) {
		BUG_ON(owner < BTRFS_FIRST_FREE_OBJECTID);
		ret = update_inline_extent_backref(trans, root, path, iref,
						   refs_to_add);
	} else if (ret == -ENOENT) {
		ret = setup_inline_extent_backref(trans, root, path, iref,
						  parent, root_objectid,
						  owner, offset, refs_to_add);
	}
	return ret;
}

static int insert_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 u64 bytenr, u64 parent, u64 root_objectid,
				 u64 owner, u64 offset, int refs_to_add)
{
	int ret;

	if (owner >= BTRFS_FIRST_FREE_OBJECTID) {
		ret = insert_extent_data_ref(trans, root, path, bytenr,
					     parent, root_objectid,
					     owner, offset, refs_to_add);
	} else {
		BUG_ON(refs_to_add != 1);
		ret = insert_tree_block_ref(trans, root, path, bytenr,
					    parent, root_objectid);
	}
	return ret;
}

static int remove_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref *iref,
				 int refs_to_drop, int is_data)
{
	int ret;

	BUG_ON(!is_data && refs_to_drop != 1);
	if (iref) {
		ret = update_inline_extent_backref(trans, root, path, iref,
						   -refs_to_drop);
	} else if (is_data) {
		ret = remove_extent_data_ref(trans, root, path, refs_to_drop);
	} else {
		ret = btrfs_del_item(trans, root, path);
	}
	return ret;
}

int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 bytenr, u64 num_bytes, u64 parent,
			 u64 root_objectid, u64 owner, u64 offset)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *item;
	u64 refs;
	int ret;
	int err = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 1;

	ret = insert_inline_extent_backref(trans, root->fs_info->extent_root,
					   path, bytenr, num_bytes, parent,
					   root_objectid, owner, offset, 1);
	if (ret == 0)
		goto out;

	if (ret != -EAGAIN) {
		err = ret;
		goto out;
	}
	
	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, item);
	btrfs_set_extent_refs(leaf, item, refs + 1);

	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	path->reada = 1;

	ret = insert_extent_backref(trans, root->fs_info->extent_root,
				    path, bytenr, parent, root_objectid,
				    owner, offset, 1);
	if (ret)
		err = ret;
out:
	btrfs_free_path(path);
	finish_current_insert(trans, root->fs_info->extent_root);
	del_pending_extents(trans, root->fs_info->extent_root);
	BUG_ON(err);
	return err;
}
int btrfs_lookup_extent_info(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 bytenr,
			     u64 offset, int metadata, u64 *refs, u64 *flags)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_extent_item *item;
	u32 item_size;
	u64 num_refs;
	u64 extent_flags;

	if (metadata &&
	    !btrfs_fs_incompat(root->fs_info,
			       BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA)) {
		offset = root->nodesize;
		metadata = 0;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = 1;

	key.objectid = bytenr;
	key.offset = offset;
	if (metadata)
		key.type = BTRFS_METADATA_ITEM_KEY;
	else
		key.type = BTRFS_EXTENT_ITEM_KEY;

again:
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, path,
				0, 0);
	if (ret < 0)
		goto out;

	if (ret > 0 && metadata) {
		if (path->slots[0]) {
			path->slots[0]--;
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0]);
			if (key.objectid == bytenr &&
			    key.type == BTRFS_EXTENT_ITEM_KEY &&
			    key.offset == root->nodesize)
				ret = 0;
		}

		if (ret) {
			btrfs_release_path(path);
			key.type = BTRFS_EXTENT_ITEM_KEY;
			key.offset = root->nodesize;
			metadata = 0;
			goto again;
		}
	}

	if (ret != 0) {
		ret = -EIO;
		goto out;
	}

	l = path->nodes[0];
	item_size = btrfs_item_size_nr(l, path->slots[0]);
	if (item_size >= sizeof(*item)) {
		item = btrfs_item_ptr(l, path->slots[0],
				      struct btrfs_extent_item);
		num_refs = btrfs_extent_refs(l, item);
		extent_flags = btrfs_extent_flags(l, item);
	} else {
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
			struct btrfs_extent_item_v0 *ei0;
			BUG_ON(item_size != sizeof(*ei0));
			ei0 = btrfs_item_ptr(l, path->slots[0],
					     struct btrfs_extent_item_v0);
			num_refs = btrfs_extent_refs_v0(l, ei0);
			extent_flags = BTRFS_BLOCK_FLAG_FULL_BACKREF;
#else
			BUG();
#endif
	}
	item = btrfs_item_ptr(l, path->slots[0], struct btrfs_extent_item);
	if (refs)
		*refs = num_refs;
	if (flags)
		*flags = extent_flags;
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_set_block_flags(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  u64 bytenr, int level, u64 flags)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_extent_item *item;
	u32 item_size;
	int skinny_metadata =
		btrfs_fs_incompat(root->fs_info,
				  BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = 1;

	key.objectid = bytenr;
	if (skinny_metadata) {
		key.offset = level;
		key.type = BTRFS_METADATA_ITEM_KEY;
	} else {
		key.offset = root->nodesize;
		key.type = BTRFS_EXTENT_ITEM_KEY;
	}

again:
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, path,
				0, 0);
	if (ret < 0)
		goto out;

	if (ret > 0 && skinny_metadata) {
		skinny_metadata = 0;
		if (path->slots[0]) {
			path->slots[0]--;
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0]);
			if (key.objectid == bytenr &&
			    key.offset == root->nodesize &&
			    key.type == BTRFS_EXTENT_ITEM_KEY)
				ret = 0;
		}
		if (ret) {
			btrfs_release_path(path);
			key.offset = root->nodesize;
			key.type = BTRFS_EXTENT_ITEM_KEY;
			goto again;
		}
	}

	if (ret != 0) {
		btrfs_print_leaf(root, path->nodes[0]);
		printk("failed to find block number %Lu\n",
			(unsigned long long)bytenr);
		BUG();
	}
	l = path->nodes[0];
	item_size = btrfs_item_size_nr(l, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*item)) {
		ret = convert_extent_item_v0(trans, root->fs_info->extent_root,
					     path, (u64)-1, 0);
		if (ret < 0)
			goto out;

		l = path->nodes[0];
		item_size = btrfs_item_size_nr(l, path->slots[0]);
	}
#endif
	BUG_ON(item_size < sizeof(*item));
	item = btrfs_item_ptr(l, path->slots[0], struct btrfs_extent_item);
	flags |= btrfs_extent_flags(l, item);
	btrfs_set_extent_flags(l, item, flags);
out:
	btrfs_free_path(path);
	finish_current_insert(trans, root->fs_info->extent_root);
	del_pending_extents(trans, root->fs_info->extent_root);
	return ret;
}

static int __btrfs_mod_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct extent_buffer *buf,
			   int record_parent, int inc)
{
	u64 bytenr;
	u64 num_bytes;
	u64 parent;
	u64 ref_root;
	u32 nritems;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	int i;
	int level;
	int ret = 0;
	int (*process_func)(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    u64, u64, u64, u64, u64, u64);

	ref_root = btrfs_header_owner(buf);
	nritems = btrfs_header_nritems(buf);
	level = btrfs_header_level(buf);

	if (!root->ref_cows && level == 0)
		return 0;

	if (inc)
		process_func = btrfs_inc_extent_ref;
	else
		process_func = btrfs_free_extent;

	if (record_parent)
		parent = buf->start;
	else
		parent = 0;

	for (i = 0; i < nritems; i++) {
		cond_resched();
		if (level == 0) {
			btrfs_item_key_to_cpu(buf, &key, i);
			if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
				continue;
			fi = btrfs_item_ptr(buf, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(buf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE)
				continue;
			bytenr = btrfs_file_extent_disk_bytenr(buf, fi);
			if (bytenr == 0)
				continue;
			
			num_bytes = btrfs_file_extent_disk_num_bytes(buf, fi);
			key.offset -= btrfs_file_extent_offset(buf, fi);
			ret = process_func(trans, root, bytenr, num_bytes,
					   parent, ref_root, key.objectid,
					   key.offset);
			if (ret) {
				WARN_ON(1);
				goto fail;
			}
		} else {
			bytenr = btrfs_node_blockptr(buf, i);
			num_bytes = root->nodesize;
			ret = process_func(trans, root, bytenr, num_bytes,
					   parent, ref_root, level - 1, 0);
			if (ret) {
				WARN_ON(1);
				goto fail;
			}
		}
	}
	return 0;
fail:
	WARN_ON(1);
	return ret;
}

int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int record_parent)
{
	return __btrfs_mod_ref(trans, root, buf, record_parent, 1);
}

int btrfs_dec_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int record_parent)
{
	return __btrfs_mod_ref(trans, root, buf, record_parent, 0);
}

static int write_one_cache_group(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_block_group_cache *cache)
{
	int ret;
	int pending_ret;
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	unsigned long bi;
	struct extent_buffer *leaf;

	ret = btrfs_search_slot(trans, extent_root, &cache->key, path, 0, 1);
	if (ret < 0)
		goto fail;
	BUG_ON(ret);

	leaf = path->nodes[0];
	bi = btrfs_item_ptr_offset(leaf, path->slots[0]);
	write_extent_buffer(leaf, &cache->item, bi, sizeof(cache->item));
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);
fail:
	finish_current_insert(trans, extent_root);
	pending_ret = del_pending_extents(trans, extent_root);
	if (ret)
		return ret;
	if (pending_ret)
		return pending_ret;
	return 0;

}

int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root)
{
	struct extent_io_tree *block_group_cache;
	struct btrfs_block_group_cache *cache;
	int ret;
	struct btrfs_path *path;
	u64 last = 0;
	u64 start;
	u64 end;
	u64 ptr;

	block_group_cache = &root->fs_info->block_group_cache;
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while(1) {
		ret = find_first_extent_bit(block_group_cache, last,
					    &start, &end, BLOCK_GROUP_DIRTY);
		if (ret) {
			if (last == 0)
				break;
			last = 0;
			continue;
		}

		last = end + 1;
		ret = get_state_private(block_group_cache, start, &ptr);
		BUG_ON(ret);

		clear_extent_bits(block_group_cache, start, end,
				  BLOCK_GROUP_DIRTY, GFP_NOFS);

		cache = (struct btrfs_block_group_cache *)(unsigned long)ptr;
		ret = write_one_cache_group(trans, root, path, cache);
	}
	btrfs_free_path(path);
	return 0;
}

static struct btrfs_space_info *__find_space_info(struct btrfs_fs_info *info,
						  u64 flags)
{
	struct btrfs_space_info *found;

	flags &= BTRFS_BLOCK_GROUP_TYPE_MASK;

	list_for_each_entry(found, &info->space_info, list) {
		if (found->flags & flags)
			return found;
	}
	return NULL;

}
static int update_space_info(struct btrfs_fs_info *info, u64 flags,
			     u64 total_bytes, u64 bytes_used,
			     struct btrfs_space_info **space_info)
{
	struct btrfs_space_info *found;

	found = __find_space_info(info, flags);
	if (found) {
		found->total_bytes += total_bytes;
		found->bytes_used += bytes_used;
		if (found->total_bytes < found->bytes_used) {
			fprintf(stderr, "warning, bad space info total_bytes "
				"%llu used %llu\n",
			       (unsigned long long)found->total_bytes,
			       (unsigned long long)found->bytes_used);
		}
		*space_info = found;
		return 0;
	}
	found = kmalloc(sizeof(*found), GFP_NOFS);
	if (!found)
		return -ENOMEM;

	list_add(&found->list, &info->space_info);
	found->flags = flags & BTRFS_BLOCK_GROUP_TYPE_MASK;
	found->total_bytes = total_bytes;
	found->bytes_used = bytes_used;
	found->bytes_pinned = 0;
	found->full = 0;
	*space_info = found;
	return 0;
}


static void set_avail_alloc_bits(struct btrfs_fs_info *fs_info, u64 flags)
{
	u64 extra_flags = flags & (BTRFS_BLOCK_GROUP_RAID0 |
				   BTRFS_BLOCK_GROUP_RAID1 |
				   BTRFS_BLOCK_GROUP_RAID10 |
				   BTRFS_BLOCK_GROUP_RAID5 |
				   BTRFS_BLOCK_GROUP_RAID6 |
				   BTRFS_BLOCK_GROUP_DUP);
	if (extra_flags) {
		if (flags & BTRFS_BLOCK_GROUP_DATA)
			fs_info->avail_data_alloc_bits |= extra_flags;
		if (flags & BTRFS_BLOCK_GROUP_METADATA)
			fs_info->avail_metadata_alloc_bits |= extra_flags;
		if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
			fs_info->avail_system_alloc_bits |= extra_flags;
	}
}

static int do_chunk_alloc(struct btrfs_trans_handle *trans,
			  struct btrfs_root *extent_root, u64 alloc_bytes,
			  u64 flags)
{
	struct btrfs_space_info *space_info;
	u64 thresh;
	u64 start;
	u64 num_bytes;
	int ret;

	space_info = __find_space_info(extent_root->fs_info, flags);
	if (!space_info) {
		ret = update_space_info(extent_root->fs_info, flags,
					0, 0, &space_info);
		BUG_ON(ret);
	}
	BUG_ON(!space_info);

	if (space_info->full)
		return 0;

	thresh = div_factor(space_info->total_bytes, 7);
	if ((space_info->bytes_used + space_info->bytes_pinned + alloc_bytes) <
	    thresh)
		return 0;

	if (extent_root->fs_info->avoid_meta_chunk_alloc &&
	    (flags & BTRFS_BLOCK_GROUP_METADATA))
		return 0;
	if (extent_root->fs_info->avoid_sys_chunk_alloc &&
	    (flags & BTRFS_BLOCK_GROUP_SYSTEM))
		return 0;

	ret = btrfs_alloc_chunk(trans, extent_root, &start, &num_bytes,
	                        space_info->flags);
	if (ret == -ENOSPC) {
		space_info->full = 1;
		return 0;
	}

	BUG_ON(ret);

	ret = btrfs_make_block_group(trans, extent_root, 0, space_info->flags,
		     BTRFS_FIRST_CHUNK_TREE_OBJECTID, start, num_bytes);
	BUG_ON(ret);
	return 0;
}

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      u64 bytenr, u64 num_bytes, int alloc,
			      int mark_free)
{
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	u64 total = num_bytes;
	u64 old_val;
	u64 byte_in_group;
	u64 start;
	u64 end;

	old_val = btrfs_super_bytes_used(info->super_copy);
	if (alloc)
		old_val += num_bytes;
	else
		old_val -= num_bytes;
	btrfs_set_super_bytes_used(info->super_copy, old_val);

	old_val = btrfs_root_used(&root->root_item);
	if (alloc)
		old_val += num_bytes;
	else
		old_val -= num_bytes;
	btrfs_set_root_used(&root->root_item, old_val);

	while(total) {
		cache = btrfs_lookup_block_group(info, bytenr);
		if (!cache) {
			return -1;
		}
		byte_in_group = bytenr - cache->key.objectid;
		WARN_ON(byte_in_group > cache->key.offset);
		start = cache->key.objectid;
		end = start + cache->key.offset - 1;
		set_extent_bits(&info->block_group_cache, start, end,
				BLOCK_GROUP_DIRTY, GFP_NOFS);

		old_val = btrfs_block_group_used(&cache->item);
		num_bytes = min(total, cache->key.offset - byte_in_group);

		if (alloc) {
			old_val += num_bytes;
			cache->space_info->bytes_used += num_bytes;
		} else {
			old_val -= num_bytes;
			cache->space_info->bytes_used -= num_bytes;
			if (mark_free) {
				set_extent_dirty(&info->free_space_cache,
						 bytenr, bytenr + num_bytes - 1,
						 GFP_NOFS);
			}
		}
		btrfs_set_block_group_used(&cache->item, old_val);
		total -= num_bytes;
		bytenr += num_bytes;
	}
	return 0;
}

static int update_pinned_extents(struct btrfs_root *root,
				u64 bytenr, u64 num, int pin)
{
	u64 len;
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (pin) {
		set_extent_dirty(&fs_info->pinned_extents,
				bytenr, bytenr + num - 1, GFP_NOFS);
	} else {
		clear_extent_dirty(&fs_info->pinned_extents,
				bytenr, bytenr + num - 1, GFP_NOFS);
	}
	while (num > 0) {
		cache = btrfs_lookup_block_group(fs_info, bytenr);
		if (!cache) {
			len = min((u64)root->sectorsize, num);
			goto next;
		}
		WARN_ON(!cache);
		len = min(num, cache->key.offset -
			  (bytenr - cache->key.objectid));
		if (pin) {
			cache->pinned += len;
			cache->space_info->bytes_pinned += len;
			fs_info->total_pinned += len;
		} else {
			cache->pinned -= len;
			cache->space_info->bytes_pinned -= len;
			fs_info->total_pinned -= len;
		}
next:
		bytenr += len;
		num -= len;
	}
	return 0;
}

int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct extent_io_tree *unpin)
{
	u64 start;
	u64 end;
	int ret;
	struct extent_io_tree *free_space_cache;
	free_space_cache = &root->fs_info->free_space_cache;

	while(1) {
		ret = find_first_extent_bit(unpin, 0, &start, &end,
					    EXTENT_DIRTY);
		if (ret)
			break;
		update_pinned_extents(root, start, end + 1 - start, 0);
		clear_extent_dirty(unpin, start, end, GFP_NOFS);
		set_extent_dirty(free_space_cache, start, end, GFP_NOFS);
	}
	return 0;
}
static int finish_current_insert(struct btrfs_trans_handle *trans,
				 struct btrfs_root *extent_root)
{
	u64 start;
	u64 end;
	u64 priv;
	struct btrfs_fs_info *info = extent_root->fs_info;
	struct pending_extent_op *extent_op;
	struct btrfs_key key;
	int ret;
	int skinny_metadata =
		btrfs_fs_incompat(extent_root->fs_info,
				  BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA);

	while(1) {
		ret = find_first_extent_bit(&info->extent_ins, 0, &start,
					    &end, EXTENT_LOCKED);
		if (ret)
			break;

		ret = get_state_private(&info->extent_ins, start, &priv);
		BUG_ON(ret);
		extent_op = (struct pending_extent_op *)(unsigned long)priv;

		if (extent_op->type == PENDING_EXTENT_INSERT) {
			key.objectid = start;
			if (skinny_metadata) {
				key.offset = extent_op->level;
				key.type = BTRFS_METADATA_ITEM_KEY;
			} else {
				key.offset = extent_op->num_bytes;
				key.type = BTRFS_EXTENT_ITEM_KEY;
			}
			ret = alloc_reserved_tree_block(trans, extent_root,
						extent_root->root_key.objectid,
						trans->transid,
						extent_op->flags,
						&extent_op->key,
						extent_op->level, &key);
			BUG_ON(ret);
		} else {
			BUG_ON(1);
		}

		clear_extent_bits(&info->extent_ins, start, end, EXTENT_LOCKED,
				  GFP_NOFS);
		kfree(extent_op);
	}
	return 0;
}

static int pin_down_bytes(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  u64 bytenr, u64 num_bytes, int is_data)
{
	int err = 0;
	struct extent_buffer *buf;

	if (is_data)
		goto pinit;

	buf = btrfs_find_tree_block(root, bytenr, num_bytes);
	if (!buf)
		goto pinit;

	if (btrfs_buffer_uptodate(buf, 0)) {
		u64 header_owner = btrfs_header_owner(buf);
		u64 header_transid = btrfs_header_generation(buf);
		if (header_owner != BTRFS_TREE_LOG_OBJECTID &&
		    header_transid == trans->transid &&
		    !btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN)) {
			clean_tree_block(NULL, root, buf);
			free_extent_buffer(buf);
			return 1;
		}
	}
	free_extent_buffer(buf);
pinit:
	update_pinned_extents(root, bytenr, num_bytes, 1);

	BUG_ON(err < 0);
	return 0;
}
static int __free_extent(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 bytenr, u64 num_bytes, u64 parent,
			 u64 root_objectid, u64 owner_objectid,
			 u64 owner_offset, int refs_to_drop)
{

	struct btrfs_key key;
	struct btrfs_path *path;
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_extent_inline_ref *iref;
	int ret;
	int is_data;
	int extent_slot = 0;
	int found_extent = 0;
	int num_to_del = 1;
	u32 item_size;
	u64 refs;
	int skinny_metadata =
		btrfs_fs_incompat(extent_root->fs_info,
				  BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA);

	if (root->fs_info->free_extent_hook) {
		root->fs_info->free_extent_hook(trans, root, bytenr, num_bytes,
						parent, root_objectid, owner_objectid,
						owner_offset, refs_to_drop);

	}
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 1;

	is_data = owner_objectid >= BTRFS_FIRST_FREE_OBJECTID;
	if (is_data)
		skinny_metadata = 0;
	BUG_ON(!is_data && refs_to_drop != 1);

	ret = lookup_extent_backref(trans, extent_root, path, &iref,
				    bytenr, num_bytes, parent,
				    root_objectid, owner_objectid,
				    owner_offset);
	if (ret == 0) {
		extent_slot = path->slots[0];
		while (extent_slot >= 0) {
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      extent_slot);
			if (key.objectid != bytenr)
				break;
			if (key.type == BTRFS_EXTENT_ITEM_KEY &&
			    key.offset == num_bytes) {
				found_extent = 1;
				break;
			}
			if (key.type == BTRFS_METADATA_ITEM_KEY &&
			    key.offset == owner_objectid) {
				found_extent = 1;
				break;
			}
			if (path->slots[0] - extent_slot > 5)
				break;
			extent_slot--;
		}
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		item_size = btrfs_item_size_nr(path->nodes[0], extent_slot);
		if (found_extent && item_size < sizeof(*ei))
			found_extent = 0;
#endif
		if (!found_extent) {
			BUG_ON(iref);
			ret = remove_extent_backref(trans, extent_root, path,
						    NULL, refs_to_drop,
						    is_data);
			BUG_ON(ret);
			btrfs_release_path(path);

			key.objectid = bytenr;

			if (skinny_metadata) {
				key.type = BTRFS_METADATA_ITEM_KEY;
				key.offset = owner_objectid;
			} else {
				key.type = BTRFS_EXTENT_ITEM_KEY;
				key.offset = num_bytes;
			}

			ret = btrfs_search_slot(trans, extent_root,
						&key, path, -1, 1);
			if (ret > 0 && skinny_metadata && path->slots[0]) {
				path->slots[0]--;
				btrfs_item_key_to_cpu(path->nodes[0],
						      &key,
						      path->slots[0]);
				if (key.objectid == bytenr &&
				    key.type == BTRFS_EXTENT_ITEM_KEY &&
				    key.offset == num_bytes)
					ret = 0;
			}

			if (ret > 0 && skinny_metadata) {
				skinny_metadata = 0;
				btrfs_release_path(path);
				key.type = BTRFS_EXTENT_ITEM_KEY;
				key.offset = num_bytes;
				ret = btrfs_search_slot(trans, extent_root,
							&key, path, -1, 1);
			}

			if (ret) {
				printk(KERN_ERR "umm, got %d back from search"
				       ", was looking for %llu\n", ret,
				       (unsigned long long)bytenr);
				btrfs_print_leaf(extent_root, path->nodes[0]);
			}
			BUG_ON(ret);
			extent_slot = path->slots[0];
		}
	} else {
		printk(KERN_ERR "btrfs unable to find ref byte nr %llu "
		       "parent %llu root %llu  owner %llu offset %llu\n",
		       (unsigned long long)bytenr,
		       (unsigned long long)parent,
		       (unsigned long long)root_objectid,
		       (unsigned long long)owner_objectid,
		       (unsigned long long)owner_offset);
		ret = -EIO;
		goto fail;
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, extent_slot);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		BUG_ON(found_extent || extent_slot != path->slots[0]);
		ret = convert_extent_item_v0(trans, extent_root, path,
					     owner_objectid, 0);
		BUG_ON(ret < 0);

		btrfs_release_path(path);

		key.objectid = bytenr;
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = num_bytes;

		ret = btrfs_search_slot(trans, extent_root, &key, path,
					-1, 1);
		if (ret) {
			printk(KERN_ERR "umm, got %d back from search"
			       ", was looking for %llu\n", ret,
			       (unsigned long long)bytenr);
			btrfs_print_leaf(extent_root, path->nodes[0]);
		}
		BUG_ON(ret);
		extent_slot = path->slots[0];
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, extent_slot);
	}
#endif
	BUG_ON(item_size < sizeof(*ei));
	ei = btrfs_item_ptr(leaf, extent_slot,
			    struct btrfs_extent_item);
	if (owner_objectid < BTRFS_FIRST_FREE_OBJECTID &&
	    key.type == BTRFS_EXTENT_ITEM_KEY) {
		struct btrfs_tree_block_info *bi;
		BUG_ON(item_size < sizeof(*ei) + sizeof(*bi));
		bi = (struct btrfs_tree_block_info *)(ei + 1);
		WARN_ON(owner_objectid != btrfs_tree_block_level(leaf, bi));
	}

	refs = btrfs_extent_refs(leaf, ei);
	BUG_ON(refs < refs_to_drop);
	refs -= refs_to_drop;

	if (refs > 0) {
		if (iref) {
			BUG_ON(!found_extent);
		} else {
			btrfs_set_extent_refs(leaf, ei, refs);
			btrfs_mark_buffer_dirty(leaf);
		}
		if (found_extent) {
			ret = remove_extent_backref(trans, extent_root, path,
						    iref, refs_to_drop,
						    is_data);
			BUG_ON(ret);
		}
	} else {
		int mark_free = 0;
		int pin = 1;

		if (found_extent) {
			BUG_ON(is_data && refs_to_drop !=
			       extent_data_ref_count(root, path, iref));
			if (iref) {
				BUG_ON(path->slots[0] != extent_slot);
			} else {
				BUG_ON(path->slots[0] != extent_slot + 1);
				path->slots[0] = extent_slot;
				num_to_del = 2;
			}
		}

		if (pin) {
			ret = pin_down_bytes(trans, root, bytenr, num_bytes,
					     is_data);
			if (ret > 0)
				mark_free = 1;
			BUG_ON(ret < 0);
		}

		ret = btrfs_del_items(trans, extent_root, path, path->slots[0],
				      num_to_del);
		BUG_ON(ret);
		btrfs_release_path(path);

		if (is_data) {
			ret = btrfs_del_csums(trans, root, bytenr, num_bytes);
			BUG_ON(ret);
		}

		update_block_group(trans, root, bytenr, num_bytes, 0, mark_free);
	}
fail:
	btrfs_free_path(path);
	finish_current_insert(trans, extent_root);
	return ret;
}

static int del_pending_extents(struct btrfs_trans_handle *trans, struct
			       btrfs_root *extent_root)
{
	int ret;
	int err = 0;
	u64 start;
	u64 end;
	u64 priv;
	struct extent_io_tree *pending_del;
	struct extent_io_tree *extent_ins;
	struct pending_extent_op *extent_op;

	extent_ins = &extent_root->fs_info->extent_ins;
	pending_del = &extent_root->fs_info->pending_del;

	while(1) {
		ret = find_first_extent_bit(pending_del, 0, &start, &end,
					    EXTENT_LOCKED);
		if (ret)
			break;

		ret = get_state_private(pending_del, start, &priv);
		BUG_ON(ret);
		extent_op = (struct pending_extent_op *)(unsigned long)priv;

		clear_extent_bits(pending_del, start, end, EXTENT_LOCKED,
				  GFP_NOFS);

		if (!test_range_bit(extent_ins, start, end,
				    EXTENT_LOCKED, 0)) {
			ret = __free_extent(trans, extent_root,
					    start, end + 1 - start, 0,
					    extent_root->root_key.objectid,
					    extent_op->level, 0, 1);
			kfree(extent_op);
		} else {
			kfree(extent_op);
			ret = get_state_private(extent_ins, start, &priv);
			BUG_ON(ret);
			extent_op = (struct pending_extent_op *)
							(unsigned long)priv;

			clear_extent_bits(extent_ins, start, end,
					  EXTENT_LOCKED, GFP_NOFS);

			if (extent_op->type == PENDING_BACKREF_UPDATE)
				BUG_ON(1);

			kfree(extent_op);
		}
		if (ret)
			err = ret;
	}
	return err;
}

int btrfs_free_extent(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      u64 bytenr, u64 num_bytes, u64 parent,
		      u64 root_objectid, u64 owner, u64 offset)
{
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	int pending_ret;
	int ret;

	WARN_ON(num_bytes < root->sectorsize);
	if (root == extent_root) {
		struct pending_extent_op *extent_op;

		extent_op = kmalloc(sizeof(*extent_op), GFP_NOFS);
		BUG_ON(!extent_op);

		extent_op->type = PENDING_EXTENT_DELETE;
		extent_op->bytenr = bytenr;
		extent_op->num_bytes = num_bytes;
		extent_op->level = (int)owner;

		set_extent_bits(&root->fs_info->pending_del,
				bytenr, bytenr + num_bytes - 1,
				EXTENT_LOCKED, GFP_NOFS);
		set_state_private(&root->fs_info->pending_del,
				  bytenr, (unsigned long)extent_op);
		return 0;
	}
	ret = __free_extent(trans, root, bytenr, num_bytes, parent,
			    root_objectid, owner, offset, 1);
	pending_ret = del_pending_extents(trans, root->fs_info->extent_root);
	return ret ? ret : pending_ret;
}


static u64 stripe_align(struct btrfs_root *root, u64 val)
{
	u64 mask = ((u64)root->stripesize - 1);
	u64 ret = (val + mask) & ~mask;
	return ret;
}

static int noinline find_free_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *orig_root,
				     u64 num_bytes, u64 empty_size,
				     u64 search_start, u64 search_end,
				     u64 hint_byte, struct btrfs_key *ins,
				     u64 exclude_start, u64 exclude_nr,
				     int data)
{
	int ret;
	u64 orig_search_start = search_start;
	struct btrfs_root * root = orig_root->fs_info->extent_root;
	struct btrfs_fs_info *info = root->fs_info;
	u64 total_needed = num_bytes;
	struct btrfs_block_group_cache *block_group;
	int full_scan = 0;
	int wrapped = 0;

	WARN_ON(num_bytes < root->sectorsize);
	btrfs_set_key_type(ins, BTRFS_EXTENT_ITEM_KEY);

	search_start = stripe_align(root, search_start);

	if (hint_byte) {
		block_group = btrfs_lookup_first_block_group(info, hint_byte);
		if (!block_group)
			hint_byte = search_start;
		block_group = btrfs_find_block_group(root, block_group,
						     hint_byte, data, 1);
	} else {
		block_group = btrfs_find_block_group(root,
						     trans->block_group,
						     search_start, data, 1);
	}

	total_needed += empty_size;

check_failed:
	search_start = stripe_align(root, search_start);
	if (!block_group) {
		block_group = btrfs_lookup_first_block_group(info,
							     search_start);
		if (!block_group)
			block_group = btrfs_lookup_first_block_group(info,
						       orig_search_start);
	}
	ret = find_search_start(root, &block_group, &search_start,
				total_needed, data);
	if (ret)
		goto new_group;

	ins->objectid = search_start;
	ins->offset = num_bytes;

	if (ins->objectid + num_bytes >
	    block_group->key.objectid + block_group->key.offset) {
		search_start = block_group->key.objectid +
			block_group->key.offset;
		goto new_group;
	}

	if (test_range_bit(&info->extent_ins, ins->objectid,
			   ins->objectid + num_bytes -1, EXTENT_LOCKED, 0)) {
		search_start = ins->objectid + num_bytes;
		goto new_group;
	}

	if (test_range_bit(&info->pinned_extents, ins->objectid,
			   ins->objectid + num_bytes -1, EXTENT_DIRTY, 0)) {
		search_start = ins->objectid + num_bytes;
		goto new_group;
	}

	if (info->excluded_extents &&
	    test_range_bit(info->excluded_extents, ins->objectid,
			   ins->objectid + num_bytes -1, EXTENT_DIRTY, 0)) {
		search_start = ins->objectid + num_bytes;
		goto new_group;
	}

	if (exclude_nr > 0 && (ins->objectid + num_bytes > exclude_start &&
	    ins->objectid < exclude_start + exclude_nr)) {
		search_start = exclude_start + exclude_nr;
		goto new_group;
	}

	if (!(data & BTRFS_BLOCK_GROUP_DATA)) {
		if (check_crossing_stripes(ins->objectid, num_bytes)) {
			search_start = round_down(ins->objectid + num_bytes,
						  BTRFS_STRIPE_LEN);
			goto new_group;
		}
		block_group = btrfs_lookup_block_group(info, ins->objectid);
		if (block_group)
			trans->block_group = block_group;
	}
	ins->offset = num_bytes;
	return 0;

new_group:
	block_group = btrfs_lookup_first_block_group(info, search_start);
	if (!block_group) {
		search_start = orig_search_start;
		if (full_scan) {
			ret = -ENOSPC;
			goto error;
		}
		if (wrapped) {
			if (!full_scan)
				total_needed -= empty_size;
			full_scan = 1;
		} else
			wrapped = 1;
	}
	cond_resched();
	block_group = btrfs_find_block_group(root, block_group,
					     search_start, data, 0);
	goto check_failed;

error:
	return ret;
}

int btrfs_reserve_extent(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 num_bytes, u64 empty_size,
			 u64 hint_byte, u64 search_end,
			 struct btrfs_key *ins, int data)
{
	int ret;
	u64 search_start = 0;
	u64 alloc_profile;
	struct btrfs_fs_info *info = root->fs_info;

	if (data) {
		alloc_profile = info->avail_data_alloc_bits &
			        info->data_alloc_profile;
		data = BTRFS_BLOCK_GROUP_DATA | alloc_profile;
	} else if ((info->system_allocs > 0 || root == info->chunk_root) &&
		   info->system_allocs >= 0) {
		alloc_profile = info->avail_system_alloc_bits &
			        info->system_alloc_profile;
		data = BTRFS_BLOCK_GROUP_SYSTEM | alloc_profile;
	} else {
		alloc_profile = info->avail_metadata_alloc_bits &
			        info->metadata_alloc_profile;
		data = BTRFS_BLOCK_GROUP_METADATA | alloc_profile;
	}

	if (root->ref_cows) {
		if (!(data & BTRFS_BLOCK_GROUP_METADATA)) {
			ret = do_chunk_alloc(trans, root->fs_info->extent_root,
					     num_bytes,
					     BTRFS_BLOCK_GROUP_METADATA);
			BUG_ON(ret);
		}
		ret = do_chunk_alloc(trans, root->fs_info->extent_root,
				     num_bytes + 2 * 1024 * 1024, data);
		BUG_ON(ret);
	}

	WARN_ON(num_bytes < root->sectorsize);
	ret = find_free_extent(trans, root, num_bytes, empty_size,
			       search_start, search_end, hint_byte, ins,
			       trans->alloc_exclude_start,
			       trans->alloc_exclude_nr, data);
	BUG_ON(ret);
	clear_extent_dirty(&root->fs_info->free_space_cache,
			   ins->objectid, ins->objectid + ins->offset - 1,
			   GFP_NOFS);
	return ret;
}
static int alloc_reserved_tree_block(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 root_objectid, u64 generation,
				     u64 flags, struct btrfs_disk_key *key,
				     int level, struct btrfs_key *ins)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_extent_item *extent_item;
	struct btrfs_tree_block_info *block_info;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	u32 size = sizeof(*extent_item) + sizeof(*iref);
	int skinny_metadata =
		btrfs_fs_incompat(fs_info,
				  BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA);

	if (!skinny_metadata)
		size += sizeof(*block_info);

	path = btrfs_alloc_path();
	BUG_ON(!path);

	ret = btrfs_insert_empty_item(trans, fs_info->extent_root, path,
				      ins, size);
	BUG_ON(ret);

	leaf = path->nodes[0];
	extent_item = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, extent_item, 1);
	btrfs_set_extent_generation(leaf, extent_item, generation);
	btrfs_set_extent_flags(leaf, extent_item,
			       flags | BTRFS_EXTENT_FLAG_TREE_BLOCK);

	if (skinny_metadata) {
		iref = (struct btrfs_extent_inline_ref *)(extent_item + 1);
	} else {
		block_info = (struct btrfs_tree_block_info *)(extent_item + 1);
		btrfs_set_tree_block_key(leaf, block_info, key);
		btrfs_set_tree_block_level(leaf, block_info, level);
		iref = (struct btrfs_extent_inline_ref *)(block_info + 1);
	}

	btrfs_set_extent_inline_ref_type(leaf, iref, BTRFS_TREE_BLOCK_REF_KEY);
	btrfs_set_extent_inline_ref_offset(leaf, iref, root_objectid);

	btrfs_mark_buffer_dirty(leaf);
	btrfs_free_path(path);

	ret = update_block_group(trans, root, ins->objectid, root->nodesize,
				 1, 0);
	return ret;
}

static int alloc_tree_block(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root, u64 num_bytes,
			    u64 root_objectid, u64 generation,
			    u64 flags, struct btrfs_disk_key *key,
			    int level, u64 empty_size, u64 hint_byte,
			    u64 search_end, struct btrfs_key *ins)
{
	int ret;
	ret = btrfs_reserve_extent(trans, root, num_bytes, empty_size,
				   hint_byte, search_end, ins, 0);
	BUG_ON(ret);

	if (root_objectid == BTRFS_EXTENT_TREE_OBJECTID) {
		struct pending_extent_op *extent_op;

		extent_op = kmalloc(sizeof(*extent_op), GFP_NOFS);
		BUG_ON(!extent_op);

		extent_op->type = PENDING_EXTENT_INSERT;
		extent_op->bytenr = ins->objectid;
		extent_op->num_bytes = ins->offset;
		extent_op->level = level;
		extent_op->flags = flags;
		memcpy(&extent_op->key, key, sizeof(*key));

		set_extent_bits(&root->fs_info->extent_ins, ins->objectid,
				ins->objectid + ins->offset - 1,
				EXTENT_LOCKED, GFP_NOFS);
		set_state_private(&root->fs_info->extent_ins,
				  ins->objectid, (unsigned long)extent_op);
	} else {
		if (btrfs_fs_incompat(root->fs_info,
				BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA)) {
			ins->offset = level;
			ins->type = BTRFS_METADATA_ITEM_KEY;
		}
		ret = alloc_reserved_tree_block(trans, root, root_objectid,
						generation, flags,
						key, level, ins);
		finish_current_insert(trans, root->fs_info->extent_root);
		del_pending_extents(trans, root->fs_info->extent_root);
	}
	return ret;
}

struct extent_buffer *btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					u32 blocksize, u64 root_objectid,
					struct btrfs_disk_key *key, int level,
					u64 hint, u64 empty_size)
{
	struct btrfs_key ins;
	int ret;
	struct extent_buffer *buf;

	ret = alloc_tree_block(trans, root, blocksize, root_objectid,
			       trans->transid, 0, key, level,
			       empty_size, hint, (u64)-1, &ins);
	if (ret) {
		BUG_ON(ret > 0);
		return ERR_PTR(ret);
	}

	buf = btrfs_find_create_tree_block(root->fs_info, ins.objectid,
					   blocksize);
	if (!buf) {
		btrfs_free_extent(trans, root, ins.objectid, ins.offset,
				  0, root->root_key.objectid, level, 0);
		BUG_ON(1);
		return ERR_PTR(-ENOMEM);
	}
	btrfs_set_buffer_uptodate(buf);
	trans->blocks_used++;

	return buf;
}

int btrfs_free_block_groups(struct btrfs_fs_info *info)
{
	struct btrfs_space_info *sinfo;
	struct btrfs_block_group_cache *cache;
	u64 start;
	u64 end;
	u64 ptr;
	int ret;

	while(1) {
		ret = find_first_extent_bit(&info->block_group_cache, 0,
					    &start, &end, (unsigned int)-1);
		if (ret)
			break;
		ret = get_state_private(&info->block_group_cache, start, &ptr);
		if (!ret) {
			cache = u64_to_ptr(ptr);
			if (cache->free_space_ctl) {
				btrfs_remove_free_space_cache(cache);
				kfree(cache->free_space_ctl);
			}
			kfree(cache);
		}
		clear_extent_bits(&info->block_group_cache, start,
				  end, (unsigned int)-1, GFP_NOFS);
	}
	while(1) {
		ret = find_first_extent_bit(&info->free_space_cache, 0,
					    &start, &end, EXTENT_DIRTY);
		if (ret)
			break;
		clear_extent_dirty(&info->free_space_cache, start,
				   end, GFP_NOFS);
	}

	while (!list_empty(&info->space_info)) {
		sinfo = list_entry(info->space_info.next,
				   struct btrfs_space_info, list);
		list_del_init(&sinfo->list);
		kfree(sinfo);
	}
	return 0;
}

static int find_first_block_group(struct btrfs_root *root,
		struct btrfs_path *path, struct btrfs_key *key)
{
	int ret;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	int slot;

	ret = btrfs_search_slot(NULL, root, key, path, 0, 0);
	if (ret < 0)
		return ret;
	while(1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto error;
			break;
		}
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid >= key->objectid &&
		    found_key.type == BTRFS_BLOCK_GROUP_ITEM_KEY)
			return 0;
		path->slots[0]++;
	}
	ret = -ENOENT;
error:
	return ret;
}

static void account_super_bytes(struct btrfs_fs_info *fs_info,
				struct btrfs_block_group_cache *cache)
{
	u64 bytenr;
	u64 *logical;
	int stripe_len;
	int i, nr, ret;

	if (cache->key.objectid < BTRFS_SUPER_INFO_OFFSET) {
		stripe_len = BTRFS_SUPER_INFO_OFFSET - cache->key.objectid;
		cache->bytes_super += stripe_len;
	}

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		bytenr = btrfs_sb_offset(i);
		ret = btrfs_rmap_block(&fs_info->mapping_tree,
				       cache->key.objectid, bytenr,
				       0, &logical, &nr, &stripe_len);
		if (ret)
			return;

		while (nr--) {
			u64 start, len;

			if (logical[nr] > cache->key.objectid +
			    cache->key.offset)
				continue;

			if (logical[nr] + stripe_len <= cache->key.objectid)
				continue;

			start = logical[nr];
			if (start < cache->key.objectid) {
				start = cache->key.objectid;
				len = (logical[nr] + stripe_len) - start;
			} else {
				len = min_t(u64, stripe_len,
					    cache->key.objectid +
					    cache->key.offset - start);
			}

			cache->bytes_super += len;
		}

		kfree(logical);
	}
}

int btrfs_read_block_groups(struct btrfs_root *root)
{
	struct btrfs_path *path;
	int ret;
	int bit;
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_space_info *space_info;
	struct extent_io_tree *block_group_cache;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;

	block_group_cache = &info->block_group_cache;

	root = info->extent_root;
	key.objectid = 0;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_BLOCK_GROUP_ITEM_KEY);
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while(1) {
		ret = find_first_block_group(root, path, &key);
		if (ret > 0) {
			ret = 0;
			goto error;
		}
		if (ret != 0) {
			goto error;
		}
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		cache = kzalloc(sizeof(*cache), GFP_NOFS);
		if (!cache) {
			ret = -ENOMEM;
			goto error;
		}

		read_extent_buffer(leaf, &cache->item,
				   btrfs_item_ptr_offset(leaf, path->slots[0]),
				   sizeof(cache->item));
		memcpy(&cache->key, &found_key, sizeof(found_key));
		cache->cached = 0;
		cache->pinned = 0;
		key.objectid = found_key.objectid + found_key.offset;
		btrfs_release_path(path);
		cache->flags = btrfs_block_group_flags(&cache->item);
		bit = 0;
		if (cache->flags & BTRFS_BLOCK_GROUP_DATA) {
			bit = BLOCK_GROUP_DATA;
		} else if (cache->flags & BTRFS_BLOCK_GROUP_SYSTEM) {
			bit = BLOCK_GROUP_SYSTEM;
		} else if (cache->flags & BTRFS_BLOCK_GROUP_METADATA) {
			bit = BLOCK_GROUP_METADATA;
		}
		set_avail_alloc_bits(info, cache->flags);
		if (btrfs_chunk_readonly(root, cache->key.objectid))
			cache->ro = 1;

		account_super_bytes(info, cache);

		ret = update_space_info(info, cache->flags, found_key.offset,
					btrfs_block_group_used(&cache->item),
					&space_info);
		BUG_ON(ret);
		cache->space_info = space_info;

		set_extent_bits(block_group_cache, found_key.objectid,
				found_key.objectid + found_key.offset - 1,
				bit | EXTENT_LOCKED, GFP_NOFS);
		set_state_private(block_group_cache, found_key.objectid,
				  (unsigned long)cache);
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

struct btrfs_block_group_cache *
btrfs_add_block_group(struct btrfs_fs_info *fs_info, u64 bytes_used, u64 type,
		      u64 chunk_objectid, u64 chunk_offset, u64 size)
{
	int ret;
	int bit = 0;
	struct btrfs_block_group_cache *cache;
	struct extent_io_tree *block_group_cache;

	block_group_cache = &fs_info->block_group_cache;

	cache = kzalloc(sizeof(*cache), GFP_NOFS);
	BUG_ON(!cache);
	cache->key.objectid = chunk_offset;
	cache->key.offset = size;

	btrfs_set_key_type(&cache->key, BTRFS_BLOCK_GROUP_ITEM_KEY);
	btrfs_set_block_group_used(&cache->item, bytes_used);
	btrfs_set_block_group_chunk_objectid(&cache->item, chunk_objectid);
	cache->flags = type;
	btrfs_set_block_group_flags(&cache->item, type);

	account_super_bytes(fs_info, cache);
	ret = update_space_info(fs_info, cache->flags, size, bytes_used,
				&cache->space_info);
	BUG_ON(ret);

	bit = block_group_state_bits(type);
	ret = set_extent_bits(block_group_cache, chunk_offset,
			      chunk_offset + size - 1,
			      bit | EXTENT_LOCKED, GFP_NOFS);
	BUG_ON(ret);

	ret = set_state_private(block_group_cache, chunk_offset,
				(unsigned long)cache);
	BUG_ON(ret);
	set_avail_alloc_bits(fs_info, type);

	return cache;
}

int btrfs_make_block_group(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, u64 bytes_used,
			   u64 type, u64 chunk_objectid, u64 chunk_offset,
			   u64 size)
{
	int ret;
	struct btrfs_root *extent_root;
	struct btrfs_block_group_cache *cache;

	cache = btrfs_add_block_group(root->fs_info, bytes_used, type,
				      chunk_objectid, chunk_offset, size);
	extent_root = root->fs_info->extent_root;
	ret = btrfs_insert_item(trans, extent_root, &cache->key, &cache->item,
				sizeof(cache->item));
	BUG_ON(ret);

	ret = finish_current_insert(trans, extent_root);
	BUG_ON(ret);
	ret = del_pending_extents(trans, extent_root);
	BUG_ON(ret);

	return 0;
}
