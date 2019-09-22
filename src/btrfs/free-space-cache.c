/*
 * Copyright (C) 2008 Red Hat.  All rights reserved.
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

#include <btrfs/crc32c.h>
#include <btrfs/kerncompat.h>
#include "free-space-cache.h"
#include "transaction.h"
#include "disk-io.h"
#include "extent_io.h"
#include "bitops.h"
#include "utils.h"


void unlink_free_space(struct btrfs_free_space_ctl *ctl,
		       struct btrfs_free_space *info)
{
	rb_erase(&info->offset_index, &ctl->free_space_offset);
	ctl->free_extents--;
	ctl->free_space -= info->bytes;
}

void __btrfs_remove_free_space_cache(struct btrfs_free_space_ctl *ctl)
{
	struct btrfs_free_space *info;
	struct rb_node *node;

	while ((node = rb_last(&ctl->free_space_offset)) != NULL) {
		info = rb_entry(node, struct btrfs_free_space, offset_index);
		unlink_free_space(ctl, info);
		free(info->bitmap);
		free(info);
	}
}

void btrfs_remove_free_space_cache(struct btrfs_block_group_cache *block_group)
{
	__btrfs_remove_free_space_cache(block_group->free_space_ctl);
}

