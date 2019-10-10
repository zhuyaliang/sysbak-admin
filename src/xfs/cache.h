#ifndef __CACHE_H__
#define __CACHE_H__

#include <btrfs/list.h>

#define CACHE_MISCOMPARE_PURGE	(1 << 0)

enum {
	CACHE_HIT,
	CACHE_MISS,
	CACHE_PURGE,
};

#define	HASH_CACHE_RATIO	8
#define CACHE_PREFETCH_PRIORITY	8
#define CACHE_MAX_PRIORITY	15
#define CACHE_DIRTY_PRIORITY	(CACHE_MAX_PRIORITY + 1)

struct cache;
struct cache_node;

typedef void *cache_key_t;

typedef void (*cache_walk_t)(struct cache_node *);
typedef struct cache_node * (*cache_node_alloc_t)(cache_key_t);
typedef int (*cache_node_flush_t)(struct cache_node *);
typedef void (*cache_node_relse_t)(struct cache_node *);
typedef unsigned int (*cache_node_hash_t)(cache_key_t, unsigned int,
					  unsigned int);
typedef int (*cache_node_compare_t)(struct cache_node *, cache_key_t);
typedef unsigned int (*cache_bulk_relse_t)(struct cache *, struct list_head *);

struct cache_operations {
	cache_node_hash_t	hash;
	cache_node_alloc_t	alloc;
	cache_node_flush_t	flush;
	cache_node_relse_t	relse;
	cache_node_compare_t	compare;
	cache_bulk_relse_t	bulkrelse;	/* optional */
};

struct cache_hash {
	struct list_head	ch_list;	/* hash chain head */
	unsigned int		ch_count;	/* hash chain length */
	pthread_mutex_t		ch_mutex;	/* hash chain mutex */
};

struct cache_mru {
	struct list_head	cm_list;	/* MRU head */
	unsigned int		cm_count;	/* MRU length */
	pthread_mutex_t		cm_mutex;	/* MRU lock */
};

struct cache_node {
	struct list_head	cn_hash;	/* hash chain */
	struct list_head	cn_mru;		/* MRU chain */
	unsigned int		cn_count;	/* reference count */
	unsigned int		cn_hashidx;	/* hash chain index */
	int			cn_priority;	/* priority, -1 = free list */
	int			cn_old_priority;/* saved pre-dirty prio */
	pthread_mutex_t		cn_mutex;	/* node mutex */
};

struct cache {
	int			c_flags;	/* behavioural flags */
	unsigned int		c_maxcount;	/* max cache nodes */
	unsigned int		c_count;	/* count of nodes */
	pthread_mutex_t		c_mutex;	/* node count mutex */
	cache_node_hash_t	hash;		/* node hash function */
	cache_node_alloc_t	alloc;		/* allocation function */
	cache_node_flush_t	flush;		/* flush dirty data function */
	cache_node_relse_t	relse;		/* memory free function */
	cache_node_compare_t	compare;	/* comparison routine */
	cache_bulk_relse_t	bulkrelse;	/* bulk release routine */
	unsigned int		c_hashsize;	/* hash bucket count */
	unsigned int		c_hashshift;	/* hash key shift */
	struct cache_hash	*c_hash;	/* hash table buckets */
	struct cache_mru	c_mrus[CACHE_DIRTY_PRIORITY + 1];
	unsigned long long	c_misses;	/* cache misses */
	unsigned long long	c_hits;		/* cache hits */
	unsigned int 		c_max;		/* max nodes ever used */
};

struct cache *cache_init(int, unsigned int, struct cache_operations *);
void cache_destroy(struct cache *);
void cache_walk(struct cache *, cache_walk_t);
void cache_purge(struct cache *);
void cache_flush(struct cache *);

int cache_node_get(struct cache *, cache_key_t, struct cache_node **);
void cache_node_put(struct cache *, struct cache_node *);
void cache_node_set_priority(struct cache *, struct cache_node *, int);
int cache_node_get_priority(struct cache_node *);

#endif	/* __CACHE_H__ */
