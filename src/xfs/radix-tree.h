/*
 * Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __XFS_SUPPORT_RADIX_TREE_H__
#define __XFS_SUPPORT_RADIX_TREE_H__

#define RADIX_TREE_TAGS

struct radix_tree_root {
	unsigned int		height;
	struct radix_tree_node	*rnode;
};

#define RADIX_TREE_INIT(mask)	{					\
	.height = 0,							\
	.rnode = NULL,							\
}

#define RADIX_TREE(name, mask) \
	struct radix_tree_root name = RADIX_TREE_INIT(mask)

#define INIT_RADIX_TREE(root, mask)					\
do {									\
	(root)->height = 0;						\
	(root)->rnode = NULL;						\
} while (0)

#ifdef RADIX_TREE_TAGS
#define RADIX_TREE_MAX_TAGS 2
#endif

int radix_tree_insert(struct radix_tree_root *, unsigned long, void *);
void radix_tree_init(void);
void *radix_tree_delete(struct radix_tree_root *, unsigned long);
void *radix_tree_lookup(struct radix_tree_root *root, unsigned long index);

#endif /* __XFS_SUPPORT_RADIX_TREE_H__ */
