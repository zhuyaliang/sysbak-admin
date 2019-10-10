/*
 * Copyright (c) 2006 Silicon Graphics, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_mount.h"
#include "xfs_bit.h"

#define CACHE_DEBUG 1
#undef CACHE_DEBUG
#define CACHE_DEBUG 1
#undef CACHE_ABORT

#define CACHE_SHAKE_COUNT	64

static unsigned int cache_generic_bulkrelse(struct cache *, struct list_head *);

struct cache *
cache_init(
	int			flags,
	unsigned int		hashsize,
	struct cache_operations	*cache_operations)
{
	struct cache *		cache;
	unsigned int		i, maxcount;

	maxcount = hashsize * HASH_CACHE_RATIO;

	if (!(cache = malloc(sizeof(struct cache))))
		return NULL;
	if (!(cache->c_hash = calloc(hashsize, sizeof(struct cache_hash)))) {
		free(cache);
		return NULL;
	}

	cache->c_flags = flags;
	cache->c_count = 0;
	cache->c_max = 0;
	cache->c_hits = 0;
	cache->c_misses = 0;
	cache->c_maxcount = maxcount;
	cache->c_hashsize = hashsize;
	cache->c_hashshift = xfs_highbit32(hashsize);
	cache->hash = cache_operations->hash;
	cache->alloc = cache_operations->alloc;
	cache->flush = cache_operations->flush;
	cache->relse = cache_operations->relse;
	cache->compare = cache_operations->compare;
	cache->bulkrelse = cache_operations->bulkrelse ?
		cache_operations->bulkrelse : cache_generic_bulkrelse;
	pthread_mutex_init(&cache->c_mutex, NULL);

	for (i = 0; i < hashsize; i++) {
		list_head_init(&cache->c_hash[i].ch_list);
		cache->c_hash[i].ch_count = 0;
		pthread_mutex_init(&cache->c_hash[i].ch_mutex, NULL);
	}

	for (i = 0; i <= CACHE_DIRTY_PRIORITY; i++) {
		list_head_init(&cache->c_mrus[i].cm_list);
		cache->c_mrus[i].cm_count = 0;
		pthread_mutex_init(&cache->c_mrus[i].cm_mutex, NULL);
	}
	return cache;
}

void
cache_expand(
	struct cache *		cache)
{
	pthread_mutex_lock(&cache->c_mutex);
	cache->c_maxcount *= 2;
	pthread_mutex_unlock(&cache->c_mutex);
}

void
cache_walk(
	struct cache *		cache,
	cache_walk_t		visit)
{
	struct cache_hash *	hash;
	struct list_head *	head;
	struct list_head *	pos;
	unsigned int		i;

	for (i = 0; i < cache->c_hashsize; i++) {
		hash = &cache->c_hash[i];
		head = &hash->ch_list;
		pthread_mutex_lock(&hash->ch_mutex);
		for (pos = head->next; pos != head; pos = pos->next)
			visit((struct cache_node *)pos);
		pthread_mutex_unlock(&hash->ch_mutex);
	}
}

#ifdef CACHE_ABORT
#define cache_abort()	abort()
#else
#define cache_abort()	do { } while (0)
#endif

#ifdef CACHE_DEBUG
static void
cache_zero_check(
	struct cache_node *	node)
{
	if (node->cn_count > 0) {
		cache_abort();
	}
}
#define cache_destroy_check(c)	cache_walk((c), cache_zero_check)
#else
#define cache_destroy_check(c)	do { } while (0)
#endif
void
cache_destroy(
	struct cache *		cache)
{
	unsigned int		i;

	cache_destroy_check(cache);
	for (i = 0; i < cache->c_hashsize; i++) {
		list_head_destroy(&cache->c_hash[i].ch_list);
		pthread_mutex_destroy(&cache->c_hash[i].ch_mutex);
	}
	for (i = 0; i <= CACHE_DIRTY_PRIORITY; i++) {
		list_head_destroy(&cache->c_mrus[i].cm_list);
		pthread_mutex_destroy(&cache->c_mrus[i].cm_mutex);
	}
	pthread_mutex_destroy(&cache->c_mutex);
	free(cache->c_hash);
	free(cache);
}

static unsigned int
cache_generic_bulkrelse(
	struct cache *		cache,
	struct list_head *	list)
{
	struct cache_node *	node;
	unsigned int		count = 0;

	while (!list_empty(list)) {
		node = list_entry(list->next, struct cache_node, cn_mru);
		pthread_mutex_destroy(&node->cn_mutex);
		list_del_init(&node->cn_mru);
		cache->relse(node);
		count++;
	}

	return count;
}

static void
cache_add_to_dirty_mru(
	struct cache		*cache,
	struct cache_node	*node)
{
	struct cache_mru	*mru = &cache->c_mrus[CACHE_DIRTY_PRIORITY];

	pthread_mutex_lock(&mru->cm_mutex);
	node->cn_old_priority = node->cn_priority;
	node->cn_priority = CACHE_DIRTY_PRIORITY;
	list_add(&node->cn_mru, &mru->cm_list);
	mru->cm_count++;
	pthread_mutex_unlock(&mru->cm_mutex);
}

static unsigned int
cache_shake(
	struct cache *		cache,
	unsigned int		priority,
	bool			purge)
{
	struct cache_mru	*mru;
	struct cache_hash *	hash;
	struct list_head	temp;
	struct list_head *	head;
	struct list_head *	pos;
	struct list_head *	n;
	struct cache_node *	node;
	unsigned int		count;

	ASSERT(priority <= CACHE_DIRTY_PRIORITY);
	if (priority > CACHE_MAX_PRIORITY && !purge)
		priority = 0;

	mru = &cache->c_mrus[priority];
	count = 0;
	list_head_init(&temp);
	head = &mru->cm_list;

	pthread_mutex_lock(&mru->cm_mutex);
	for (pos = head->prev, n = pos->prev; pos != head;
						pos = n, n = pos->prev) {
		node = list_entry(pos, struct cache_node, cn_mru);

		if (pthread_mutex_trylock(&node->cn_mutex) != 0)
			continue;

		if (cache->flush(node) && !purge) {
			list_del(&node->cn_mru);
			mru->cm_count--;
			node->cn_priority = -1;
			pthread_mutex_unlock(&node->cn_mutex);
			cache_add_to_dirty_mru(cache, node);
			continue;
		}

		hash = cache->c_hash + node->cn_hashidx;
		if (pthread_mutex_trylock(&hash->ch_mutex) != 0) {
			pthread_mutex_unlock(&node->cn_mutex);
			continue;
		}
		ASSERT(node->cn_count == 0);
		ASSERT(node->cn_priority == priority);
		node->cn_priority = -1;

		list_move(&node->cn_mru, &temp);
		list_del_init(&node->cn_hash);
		hash->ch_count--;
		mru->cm_count--;
		pthread_mutex_unlock(&hash->ch_mutex);
		pthread_mutex_unlock(&node->cn_mutex);

		count++;
		if (!purge && count == CACHE_SHAKE_COUNT)
			break;
	}
	pthread_mutex_unlock(&mru->cm_mutex);

	if (count > 0) {
		cache->bulkrelse(cache, &temp);

		pthread_mutex_lock(&cache->c_mutex);
		cache->c_count -= count;
		pthread_mutex_unlock(&cache->c_mutex);
	}

	return (count == CACHE_SHAKE_COUNT) ? priority : ++priority;
}

static struct cache_node *
cache_node_allocate(
	struct cache *		cache,
	cache_key_t		key)
{
	unsigned int		nodesfree;
	struct cache_node *	node;

	pthread_mutex_lock(&cache->c_mutex);
	nodesfree = (cache->c_count < cache->c_maxcount);
	if (nodesfree) {
		cache->c_count++;
		if (cache->c_count > cache->c_max)
			cache->c_max = cache->c_count;
	}
	cache->c_misses++;
	pthread_mutex_unlock(&cache->c_mutex);
	if (!nodesfree)
		return NULL;
	node = cache->alloc(key);
	if (node == NULL) {
		pthread_mutex_lock(&cache->c_mutex);
		cache->c_count--;
		pthread_mutex_unlock(&cache->c_mutex);
		return NULL;
	}
	pthread_mutex_init(&node->cn_mutex, NULL);
	list_head_init(&node->cn_mru);
	node->cn_count = 1;
	node->cn_priority = 0;
	node->cn_old_priority = -1;
	return node;
}
static int
__cache_node_purge(
	struct cache *		cache,
	struct cache_node *	node)
{
	int			count;
	struct cache_mru *	mru;

	pthread_mutex_lock(&node->cn_mutex);
	count = node->cn_count;
	if (count != 0) {
		pthread_mutex_unlock(&node->cn_mutex);
		return count;
	}

	if (cache->flush(node)) {
		pthread_mutex_unlock(&node->cn_mutex);
		return 1;
	}

	mru = &cache->c_mrus[node->cn_priority];
	pthread_mutex_lock(&mru->cm_mutex);
	list_del_init(&node->cn_mru);
	mru->cm_count--;
	pthread_mutex_unlock(&mru->cm_mutex);

	pthread_mutex_unlock(&node->cn_mutex);
	pthread_mutex_destroy(&node->cn_mutex);
	list_del_init(&node->cn_hash);
	cache->relse(node);
	return 0;
}
int
cache_node_get(
	struct cache *		cache,
	cache_key_t		key,
	struct cache_node **	nodep)
{
	struct cache_node *	node = NULL;
	struct cache_hash *	hash;
	struct cache_mru *	mru;
	struct list_head *	head;
	struct list_head *	pos;
	struct list_head *	n;
	unsigned int		hashidx;
	int			priority = 0;
	int			purged = 0;

	hashidx = cache->hash(key, cache->c_hashsize, cache->c_hashshift);
	hash = cache->c_hash + hashidx;
	head = &hash->ch_list;

	for (;;) {
		pthread_mutex_lock(&hash->ch_mutex);
		for (pos = head->next, n = pos->next; pos != head;
						pos = n, n = pos->next) {
			int result;

			node = list_entry(pos, struct cache_node, cn_hash);
			result = cache->compare(node, key);
			switch (result) {
			case CACHE_HIT:
				break;
			case CACHE_PURGE:
				if ((cache->c_flags & CACHE_MISCOMPARE_PURGE) &&
				    !__cache_node_purge(cache, node)) {
					purged++;
					hash->ch_count--;
				}
			case CACHE_MISS:
				goto next_object;
			}

			pthread_mutex_lock(&node->cn_mutex);

			if (node->cn_count == 0) {
				ASSERT(node->cn_priority >= 0);
				ASSERT(!list_empty(&node->cn_mru));
				mru = &cache->c_mrus[node->cn_priority];
				pthread_mutex_lock(&mru->cm_mutex);
				mru->cm_count--;
				list_del_init(&node->cn_mru);
				pthread_mutex_unlock(&mru->cm_mutex);
				if (node->cn_old_priority != -1) {
					ASSERT(node->cn_priority ==
							CACHE_DIRTY_PRIORITY);
					node->cn_priority = node->cn_old_priority;
					node->cn_old_priority = -1;
				}
			}
			node->cn_count++;

			pthread_mutex_unlock(&node->cn_mutex);
			pthread_mutex_unlock(&hash->ch_mutex);

			pthread_mutex_lock(&cache->c_mutex);
			cache->c_hits++;
			pthread_mutex_unlock(&cache->c_mutex);

			*nodep = node;
			return 0;
next_object:
			continue;
		}
		pthread_mutex_unlock(&hash->ch_mutex);
		node = cache_node_allocate(cache, key);
		if (node)
			break;
		priority = cache_shake(cache, priority, false);
		if (priority > CACHE_MAX_PRIORITY) {
			priority = 0;
			cache_expand(cache);
		}
	}

	node->cn_hashidx = hashidx;

	pthread_mutex_lock(&hash->ch_mutex);
	hash->ch_count++;
	list_add(&node->cn_hash, &hash->ch_list);
	pthread_mutex_unlock(&hash->ch_mutex);

	if (purged) {
		pthread_mutex_lock(&cache->c_mutex);
		cache->c_count -= purged;
		pthread_mutex_unlock(&cache->c_mutex);
	}

	*nodep = node;
	return 1;
}
void
cache_node_put(
	struct cache *		cache,
	struct cache_node *	node)
{
	struct cache_mru *	mru;

	pthread_mutex_lock(&node->cn_mutex);
	node->cn_count--;

	if (node->cn_count == 0) {
		mru = &cache->c_mrus[node->cn_priority];
		pthread_mutex_lock(&mru->cm_mutex);
		mru->cm_count++;
		list_add(&node->cn_mru, &mru->cm_list);
		pthread_mutex_unlock(&mru->cm_mutex);
	}

	pthread_mutex_unlock(&node->cn_mutex);
}

void
cache_node_set_priority(
	struct cache *		cache,
	struct cache_node *	node,
	int			priority)
{
	if (priority < 0)
		priority = 0;
	else if (priority > CACHE_MAX_PRIORITY)
		priority = CACHE_MAX_PRIORITY;

	pthread_mutex_lock(&node->cn_mutex);
	ASSERT(node->cn_count > 0);
	node->cn_priority = priority;
	node->cn_old_priority = -1;
	pthread_mutex_unlock(&node->cn_mutex);
}

int
cache_node_get_priority(
	struct cache_node *	node)
{
	int			priority;

	pthread_mutex_lock(&node->cn_mutex);
	priority = node->cn_priority;
	pthread_mutex_unlock(&node->cn_mutex);

	return priority;
}
