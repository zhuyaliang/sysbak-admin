#include "xfs_init.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include <xfs/xfs_log_format.h>
#include "xfs_mount.h"
#include "xfs_inode.h"

#include "libxfs.h"		/* for LIBXFS_EXIT_ON_FAILURE */


#define BDSTRAT_SIZE	(256 * 1024)

#define IO_BCOMPARE_CHECK
kmem_zone_t			*xfs_buf_zone;

static struct cache_mru		xfs_buf_freelist =
	{{&xfs_buf_freelist.cm_list, &xfs_buf_freelist.cm_list},
	 0, PTHREAD_MUTEX_INITIALIZER };

struct xfs_bufkey {
	struct xfs_buftarg	*buftarg;
	xfs_daddr_t		blkno;
	unsigned int		bblen;
	struct xfs_buf_map	*map;
	int			nmaps;
};

#define GOLDEN_RATIO_PRIME	0x9e37fffffffc0001UL
#define CACHE_LINE_SIZE		64
static unsigned int
libxfs_bhash(cache_key_t key, unsigned int hashsize, unsigned int hashshift)
{
	uint64_t	hashval = ((struct xfs_bufkey *)key)->blkno;
	uint64_t	tmp;

	tmp = hashval ^ (GOLDEN_RATIO_PRIME + hashval) / CACHE_LINE_SIZE;
	tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_PRIME) >> hashshift);
	return tmp % hashsize;
}

static int
libxfs_bcompare(struct cache_node *node, cache_key_t key)
{
	struct xfs_buf	*bp = (struct xfs_buf *)node;
	struct xfs_bufkey *bkey = (struct xfs_bufkey *)key;

	if (bp->b_target->dev == bkey->buftarg->dev &&
	    bp->b_bn == bkey->blkno) {
		if (bp->b_bcount == BBTOB(bkey->bblen))
			return CACHE_HIT;
		return CACHE_PURGE;
	}
	return CACHE_MISS;
}
static void
__initbuf(xfs_buf_t *bp, struct xfs_buftarg *btp, xfs_daddr_t bno,
		unsigned int bytes)
{
	bp->b_flags = 0;
	bp->b_bn = bno;
	bp->b_bcount = bytes;
	bp->b_length = BTOBB(bytes);
	bp->b_target = btp;
	bp->b_error = 0;
	if (!bp->b_addr)
		bp->b_addr = memalign(libxfs_device_alignment(), bytes);
	if (!bp->b_addr) {
		exit(1);
	}
	memset(bp->b_addr, 0, bytes);
#ifdef XFS_BUF_TRACING
	list_head_init(&bp->b_lock_list);
#endif
	pthread_mutex_init(&bp->b_lock, NULL);
	bp->b_holder = 0;
	bp->b_recur = 0;
	bp->b_ops = NULL;
}
static void
libxfs_initbuf(xfs_buf_t *bp, struct xfs_buftarg *btp, xfs_daddr_t bno,
		unsigned int bytes)
{
	__initbuf(bp, btp, bno, bytes);
}

static void
libxfs_initbuf_map(xfs_buf_t *bp, struct xfs_buftarg *btp,
		struct xfs_buf_map *map, int nmaps)
{
	unsigned int bytes = 0;
	int i;

	bytes = sizeof(struct xfs_buf_map) * nmaps;
	bp->b_maps = malloc(bytes);
	if (!bp->b_maps) {
		exit(1);
	}
	bp->b_nmaps = nmaps;

	bytes = 0;
	for ( i = 0; i < nmaps; i++) {
		bp->b_maps[i].bm_bn = map[i].bm_bn;
		bp->b_maps[i].bm_len = map[i].bm_len;
		bytes += BBTOB(map[i].bm_len);
	}

	__initbuf(bp, btp, map[0].bm_bn, bytes);
	bp->b_flags |= LIBXFS_B_DISCONTIG;
}

static void *
kmem_zone_zalloc(kmem_zone_t *zone, int flags)
{
	void	*ptr = malloc(zone->zone_unitsize);

	zone->allocated++;
	memset(ptr, 0, zone->zone_unitsize);
	return ptr;
}
static xfs_buf_t *
__libxfs_getbufr(int blen)
{
	xfs_buf_t	*bp;

	pthread_mutex_lock(&xfs_buf_freelist.cm_mutex);
	if (!list_empty(&xfs_buf_freelist.cm_list)) {
		list_for_each_entry1(bp, &xfs_buf_freelist.cm_list, b_node.cn_mru) {
			if (bp->b_bcount == (unsigned)blen) {
				list_del_init(&bp->b_node.cn_mru);
				break;
			}
		}
		if (&bp->b_node.cn_mru == &xfs_buf_freelist.cm_list) {
			bp = list_entry1(xfs_buf_freelist.cm_list.next,
					xfs_buf_t, b_node.cn_mru);
			list_del_init(&bp->b_node.cn_mru);
			free(bp->b_addr);
			bp->b_addr = NULL;
			free(bp->b_maps);
			bp->b_maps = NULL;
		}
	} else
		bp = kmem_zone_zalloc(xfs_buf_zone, 0);
	pthread_mutex_unlock(&xfs_buf_freelist.cm_mutex);
	bp->b_ops = NULL;

	return bp;
}

xfs_buf_t *
libxfs_getbufr(struct xfs_buftarg *btp, xfs_daddr_t blkno, int bblen)
{
	xfs_buf_t	*bp;
	int		blen = BBTOB(bblen);

	bp =__libxfs_getbufr(blen);
	if (bp)
		libxfs_initbuf(bp, btp, blkno, blen);

	return bp;
}
static xfs_buf_t *
libxfs_getbufr_map(struct xfs_buftarg *btp, xfs_daddr_t blkno, int bblen,
		struct xfs_buf_map *map, int nmaps)
{
	xfs_buf_t	*bp;
	int		blen = BBTOB(bblen);

	if (!map || !nmaps) {
		exit(1);
	}

	if (blkno != map[0].bm_bn) {
		exit(1);
	}

	bp =__libxfs_getbufr(blen);
	if (bp)
		libxfs_initbuf_map(bp, btp, map, nmaps);
	return bp;
}
extern int     use_xfs_buf_lock;

static struct xfs_buf *
__cache_lookup(struct xfs_bufkey *key, unsigned int flags)
{
	struct xfs_buf	*bp;

	cache_node_get(libxfs_bcache, key, (struct cache_node **)&bp);
	if (!bp)
		return NULL;

	if (use_xfs_buf_lock) {
		int ret;

		ret = pthread_mutex_trylock(&bp->b_lock);
		if (ret) {
			ASSERT(ret == EAGAIN);
			if (flags & LIBXFS_GETBUF_TRYLOCK)
				goto out_put;

			if (pthread_equal(bp->b_holder, pthread_self())) {
				bp->b_recur++;
				return bp;
			} else {
				pthread_mutex_lock(&bp->b_lock);
			}
		}

		bp->b_holder = pthread_self();
	}

	cache_node_set_priority(libxfs_bcache, (struct cache_node *)bp,
		cache_node_get_priority((struct cache_node *)bp) -
						CACHE_PREFETCH_PRIORITY);
#ifdef XFS_BUF_TRACING
	pthread_mutex_lock(&libxfs_bcache->c_mutex);
	lock_buf_count++;
	list_add(&bp->b_lock_list, &lock_buf_list);
	pthread_mutex_unlock(&libxfs_bcache->c_mutex);
#endif

	return bp;
out_put:
	cache_node_put(libxfs_bcache, (struct cache_node *)bp);
	return NULL;
}
struct xfs_buf *
libxfs_getbuf_flags(struct xfs_buftarg *btp, xfs_daddr_t blkno, int len,
		unsigned int flags)
{
	struct xfs_bufkey key = {0};

	key.buftarg = btp;
	key.blkno = blkno;
	key.bblen = len;

	return __cache_lookup(&key, flags);
}
void
libxfs_putbuf(xfs_buf_t *bp)
{
	bp->b_error = 0;

#ifdef XFS_BUF_TRACING
	pthread_mutex_lock(&libxfs_bcache->c_mutex);
	lock_buf_count--;
	ASSERT(lock_buf_count >= 0);
	list_del_init(&bp->b_lock_list);
	pthread_mutex_unlock(&libxfs_bcache->c_mutex);
#endif
	if (use_xfs_buf_lock) {
		if (bp->b_recur) {
			bp->b_recur--;
		} else {
			bp->b_holder = 0;
			pthread_mutex_unlock(&bp->b_lock);
		}
	}

	cache_node_put(libxfs_bcache, (struct cache_node *)bp);
}
static struct cache_node *
libxfs_balloc(cache_key_t key)
{
	struct xfs_bufkey *bufkey = (struct xfs_bufkey *)key;

	if (bufkey->map)
		return (struct cache_node *)
		       libxfs_getbufr_map(bufkey->buftarg,
					  bufkey->blkno, bufkey->bblen,
					  bufkey->map, bufkey->nmaps);
	return (struct cache_node *)libxfs_getbufr(bufkey->buftarg,
					  bufkey->blkno, bufkey->bblen);
}

static int
__read_buf(int fd, void *buf, int len, off64_t offset, int flags)
{
	int	sts;

	sts = pread(fd, buf, len, offset);
	if (sts < 0) {
		int error = errno;
		if (flags & LIBXFS_EXIT_ON_FAILURE)
			exit(1);
		return -error;
	} else if (sts != len) {
		if (flags & LIBXFS_EXIT_ON_FAILURE)
			exit(1);
		return -EIO;
	}
	return 0;
}

int
libxfs_readbufr(struct xfs_buftarg *btp, xfs_daddr_t blkno, xfs_buf_t *bp,
		int len, int flags)
{
	int	fd = libxfs_device_to_fd(btp->dev);
	int	bytes = BBTOB(len);
	int	error;

    printf("libxfs_readbufr = \r\n");
	error = __read_buf(fd, bp->b_addr, bytes, LIBXFS_BBTOOFF64(blkno), flags);
	if (!error &&
	    bp->b_target->dev == btp->dev &&
	    bp->b_bn == blkno &&
	    bp->b_bcount == (unsigned)bytes)
		bp->b_flags |= LIBXFS_B_UPTODATE;
	return error;
}

void
libxfs_readbuf_verify(struct xfs_buf *bp, const struct xfs_buf_ops *ops)
{
	if (!ops)
		return;
	bp->b_ops = ops;
	bp->b_ops->verify_read(bp);
	bp->b_flags &= ~LIBXFS_B_UNCHECKED;
}


xfs_buf_t *
libxfs_readbuf(struct xfs_buftarg *btp, xfs_daddr_t blkno, int len, int flags,
		const struct xfs_buf_ops *ops)
{
	xfs_buf_t	*bp;
	int		error;

	bp = libxfs_getbuf_flags(btp, blkno, len, 0);
	if (!bp)
		return NULL;

	bp->b_error = 0;
	if ((bp->b_flags & (LIBXFS_B_UPTODATE|LIBXFS_B_DIRTY))) {
		if (bp->b_flags & LIBXFS_B_UNCHECKED)
			libxfs_readbuf_verify(bp, ops);
		return bp;
	}

	error = libxfs_readbufr(btp, blkno, bp, len, flags);
	if (error)
		bp->b_error = error;
	else
		libxfs_readbuf_verify(bp, ops);
	return bp;
}
static int
__write_buf(int fd, void *buf, int len, off64_t offset, int flags)
{
	int	sts;

	sts = pwrite(fd, buf, len, offset);
	if (sts < 0) {
		int error = errno;
		if (flags & LIBXFS_B_EXIT)
			exit(1);
		return -error;
	} else if (sts != len) {
		if (flags & LIBXFS_B_EXIT)
			exit(1);
		return -EIO;
	}
	return 0;
}
int
libxfs_writebufr(xfs_buf_t *bp)
{
	int	fd = libxfs_device_to_fd(bp->b_target->dev);
	if (bp->b_flags & LIBXFS_B_STALE) {
		bp->b_error = -ESTALE;
		return bp->b_error;
	}

	bp->b_error = 0;
	if (bp->b_ops) {
		bp->b_ops->verify_write(bp);
		if (bp->b_error) {
			return bp->b_error;
		}
	}

	if (!(bp->b_flags & LIBXFS_B_DISCONTIG)) {
		bp->b_error = __write_buf(fd, bp->b_addr, bp->b_bcount,
				    LIBXFS_BBTOOFF64(bp->b_bn), bp->b_flags);
	} else {
		int	i;
		char	*buf = bp->b_addr;

		for (i = 0; i < bp->b_nmaps; i++) {
			off64_t	offset = LIBXFS_BBTOOFF64(bp->b_maps[i].bm_bn);
			int len = BBTOB(bp->b_maps[i].bm_len);

			bp->b_error = __write_buf(fd, buf, len, offset,
						  bp->b_flags);
			if (bp->b_error)
				break;
			buf += len;
		}
	}
	if (!bp->b_error) {
		bp->b_flags |= LIBXFS_B_UPTODATE;
		bp->b_flags &= ~(LIBXFS_B_DIRTY | LIBXFS_B_EXIT |
				 LIBXFS_B_UNCHECKED);
	}
	return bp->b_error;
}
static void
libxfs_brelse(
	struct cache_node	*node)
{
	struct xfs_buf		*bp = (struct xfs_buf *)node;

	if (!bp)
		return;
	if (bp->b_flags & LIBXFS_B_DIRTY)

	pthread_mutex_lock(&xfs_buf_freelist.cm_mutex);
	list_add(&bp->b_node.cn_mru, &xfs_buf_freelist.cm_list);
	pthread_mutex_unlock(&xfs_buf_freelist.cm_mutex);
}

static unsigned int
libxfs_bulkrelse(
	struct cache		*cache,
	struct list_head	*list)
{
	xfs_buf_t		*bp;
	int			count = 0;

	if (list_empty(list))
		return 0 ;

	list_for_each_entry1(bp, list, b_node.cn_mru) {
		if (bp->b_flags & LIBXFS_B_DIRTY)
		count++;
	}

	pthread_mutex_lock(&xfs_buf_freelist.cm_mutex);
	list_splice(list, &xfs_buf_freelist.cm_list);
	pthread_mutex_unlock(&xfs_buf_freelist.cm_mutex);

	return count;
}

static int
libxfs_bflush(
	struct cache_node	*node)
{
	struct xfs_buf		*bp = (struct xfs_buf *)node;

	if (!bp->b_error && bp->b_flags & LIBXFS_B_DIRTY)
		return libxfs_writebufr(bp);
	return bp->b_error;
}
struct cache_operations libxfs_bcache_operations = {
	.hash		= libxfs_bhash,
	.alloc		= libxfs_balloc,
	.flush		= libxfs_bflush,
	.relse		= libxfs_brelse,
	.compare	= libxfs_bcompare,
	.bulkrelse	= libxfs_bulkrelse
};
extern kmem_zone_t	*xfs_ili_zone;
extern kmem_zone_t	*xfs_inode_zone;
