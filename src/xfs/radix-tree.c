/*
 * Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 * Copyright (C) 2005 SGI, Christoph Lameter <clameter@sgi.com>
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

#include <xfs/xfs.h>
#include "radix-tree.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define RADIX_TREE_MAP_SHIFT	6
#define RADIX_TREE_MAP_SIZE	(1UL << RADIX_TREE_MAP_SHIFT)
#define RADIX_TREE_MAP_MASK	(RADIX_TREE_MAP_SIZE-1)

#ifdef RADIX_TREE_TAGS
#define RADIX_TREE_TAG_LONGS	\
	((RADIX_TREE_MAP_SIZE + BITS_PER_LONG - 1) / BITS_PER_LONG)
#endif

#define SIZEOF_LONG 8
#define SIZEOF_CHAR_P 8
#define BITS_PER_LONG (SIZEOF_LONG * CHAR_BIT)
struct radix_tree_node {
	unsigned int	count;
	void		*slots[RADIX_TREE_MAP_SIZE];
#ifdef RADIX_TREE_TAGS
	unsigned long	tags[RADIX_TREE_MAX_TAGS][RADIX_TREE_TAG_LONGS];
#endif
};

struct radix_tree_path {
	struct radix_tree_node *node;
	int offset;
};

#define RADIX_TREE_INDEX_BITS  (8 /* CHAR_BIT */ * sizeof(unsigned long))
#define RADIX_TREE_MAX_PATH (RADIX_TREE_INDEX_BITS/RADIX_TREE_MAP_SHIFT + 2)
static unsigned long height_to_maxindex[RADIX_TREE_MAX_PATH];
#define radix_tree_node_alloc(r) 	((struct radix_tree_node *) \
		calloc(1, sizeof(struct radix_tree_node)))
#define radix_tree_node_free(n) 	free(n)

#ifdef RADIX_TREE_TAGS

static inline void tag_set(struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	*((__uint32_t *)node->tags[tag] + (offset >> 5)) |= (1 << (offset & 31));
}

static inline void tag_clear(struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	__uint32_t 	*p = (__uint32_t*)node->tags[tag] + (offset >> 5);
	__uint32_t 	m = 1 << (offset & 31);
	*p &= ~m;
}

static inline int tag_get(struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	return 1 & (((const __uint32_t *)node->tags[tag])[offset >> 5] >> (offset & 31));
}

static inline int any_tag_set(struct radix_tree_node *node, unsigned int tag)
{
	int idx;
	for (idx = 0; idx < RADIX_TREE_TAG_LONGS; idx++) {
		if (node->tags[tag][idx])
			return 1;
	}
	return 0;
}

#endif

static inline unsigned long radix_tree_maxindex(unsigned int height)
{
	return height_to_maxindex[height];
}

static int radix_tree_extend(struct radix_tree_root *root, unsigned long index)
{
	struct radix_tree_node *node;
	unsigned int height;
#ifdef RADIX_TREE_TAGS
	char tags[RADIX_TREE_MAX_TAGS];
	int tag;
#endif
	height = root->height + 1;
	while (index > radix_tree_maxindex(height))
		height++;

	if (root->rnode == NULL) {
		root->height = height;
		goto out;
	}

#ifdef RADIX_TREE_TAGS
	for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
		tags[tag] = 0;
		if (any_tag_set(root->rnode, tag))
			tags[tag] = 1;
	}
#endif
	do {
		if (!(node = radix_tree_node_alloc(root)))
			return -ENOMEM;

		node->slots[0] = root->rnode;

#ifdef RADIX_TREE_TAGS
		for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
			if (tags[tag])
				tag_set(node, tag, 0);
		}
#endif
		node->count = 1;
		root->rnode = node;
		root->height++;
	} while (height > root->height);
out:
	return 0;
}
static inline void **__lookup_slot(struct radix_tree_root *root,
                   unsigned long index)
{
    unsigned int height, shift;
    struct radix_tree_node **slot;

    height = root->height;
    if (index > radix_tree_maxindex(height))
        return NULL;

    shift = (height-1) * RADIX_TREE_MAP_SHIFT;
    slot = &root->rnode;

    while (height > 0) {
        if (*slot == NULL)
            return NULL;

        slot = (struct radix_tree_node **)
            ((*slot)->slots +
                ((index >> shift) & RADIX_TREE_MAP_MASK));
        shift -= RADIX_TREE_MAP_SHIFT;
        height--;
    }

    return (void **)slot;
}
void *radix_tree_lookup(struct radix_tree_root *root, unsigned long index)
{
    void **slot;

    slot = __lookup_slot(root, index);
    return slot != NULL ? *slot : NULL;
}

int radix_tree_insert(struct radix_tree_root *root,
			unsigned long index, void *item)
{
	struct radix_tree_node *node = NULL, *slot;
	unsigned int height, shift;
	int offset;
	int error;

	if ((!index && !root->rnode) ||
			index > radix_tree_maxindex(root->height)) {
		error = radix_tree_extend(root, index);
		if (error)
			return error;
	}

	slot = root->rnode;
	height = root->height;
	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

	offset = 0;	
	do {
		if (slot == NULL) {
			if (!(slot = radix_tree_node_alloc(root)))
				return -ENOMEM;
			if (node) {
				node->slots[offset] = slot;
				node->count++;
			} else
				root->rnode = slot;
		}

		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		node = slot;
		slot = node->slots[offset];
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	} while (height > 0);

	if (slot != NULL)
		return -EEXIST;

	node->count++;
	node->slots[offset] = item;
	return 0;
}

static inline void radix_tree_shrink(struct radix_tree_root *root)
{
	while (root->height > 1 &&
			root->rnode->count == 1 &&
			root->rnode->slots[0]) {
		struct radix_tree_node *to_free = root->rnode;

		root->rnode = to_free->slots[0];
		root->height--;
#ifdef RADIX_TREE_TAGS
		tag_clear(to_free, 0, 0);
		tag_clear(to_free, 1, 0);
#endif
		to_free->slots[0] = NULL;
		to_free->count = 0;
		radix_tree_node_free(to_free);
	}
}

void *radix_tree_delete(struct radix_tree_root *root, unsigned long index)
{
	struct radix_tree_path path[RADIX_TREE_MAX_PATH + 1], *pathp = path;
	struct radix_tree_path *orig_pathp;
	struct radix_tree_node *slot;
	unsigned int height, shift;
	void *ret = NULL;
#ifdef RADIX_TREE_TAGS
	char tags[RADIX_TREE_MAX_TAGS];
	int nr_cleared_tags;
	int tag;
#endif
	int offset;

	height = root->height;
	if (index > radix_tree_maxindex(height))
		goto out;

	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
	pathp->node = NULL;
	slot = root->rnode;

	for ( ; height > 0; height--) {
		if (slot == NULL)
			goto out;

		pathp++;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		pathp->offset = offset;
		pathp->node = slot;
		slot = slot->slots[offset];
		shift -= RADIX_TREE_MAP_SHIFT;
	}

	ret = slot;
	if (ret == NULL)
		goto out;

	orig_pathp = pathp;

#ifdef RADIX_TREE_TAGS
	nr_cleared_tags = 0;
	for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
		tags[tag] = 1;
		if (tag_get(pathp->node, tag, pathp->offset)) {
			tag_clear(pathp->node, tag, pathp->offset);
			if (!any_tag_set(pathp->node, tag)) {
				tags[tag] = 0;
				nr_cleared_tags++;
			}
		}
	}

	for (pathp--; nr_cleared_tags && pathp->node; pathp--) {
		for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
			if (tags[tag])
				continue;

			tag_clear(pathp->node, tag, pathp->offset);
			if (any_tag_set(pathp->node, tag)) {
				tags[tag] = 1;
				nr_cleared_tags--;
			}
		}
	}
#endif
	for (pathp = orig_pathp; pathp->node; pathp--) {
		pathp->node->slots[pathp->offset] = NULL;
		pathp->node->count--;

		if (pathp->node->count) {
			if (pathp->node == root->rnode)
				radix_tree_shrink(root);
			goto out;
		}

		radix_tree_node_free(pathp->node);
	}
	root->rnode = NULL;
	root->height = 0;
out:
	return ret;
}
static unsigned long __maxindex(unsigned int height)
{
	unsigned int width = height * RADIX_TREE_MAP_SHIFT;
	int shift = RADIX_TREE_INDEX_BITS - width;

	if (shift < 0)
		return ~0UL;
	if (shift >= BITS_PER_LONG)
		return 0UL;
	return ~0UL >> shift;
}

static void radix_tree_init_maxindex(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(height_to_maxindex); i++)
		height_to_maxindex[i] = __maxindex(i);
}

void radix_tree_init(void)
{
	radix_tree_init_maxindex();
}
