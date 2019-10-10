/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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


#include "libxfs_priv.h"
#include "xfs_init.h"
#include "xfs_fs.h"
//#include "xfs_shared.h"
#include "xfs_format.h"
#include <xfs/xfs_log_format.h>
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode_buf.h"
#include "xfs_inode_fork.h"
#include "xfs_inode.h"
//#include "xfs_trans.h"

#include "libxfs.h"		/* for LIBXFS_EXIT_ON_FAILURE */

/*
 * Important design/architecture note:
 *
 * The userspace code that uses the buffer cache is much less constrained than
 * the kernel code. The userspace code is pretty nasty in places, especially
 * when it comes to buffer error handling.  Very little of the userspace code
 * outside libxfs clears bp->b_error - very little code even checks it - so the
 * libxfs code is tripping on stale errors left by the userspace code.
 *
 * We can't clear errors or zero buffer contents in libxfs_getbuf-* like we do
 * in the kernel, because those functions are used by the libxfs_readbuf_*
 * functions and hence need to leave the buffers unchanged on cache hits. This
 * is actually the only way to gather a write error from a libxfs_writebuf()
 * call - you need to get the buffer again so you can check bp->b_error field -
 * assuming that the buffer is still in the cache when you check, that is.
 *
 * This is very different to the kernel code which does not release buffers on a
 * write so we can wait on IO and check errors. The kernel buffer cache also
 * guarantees a buffer of a known initial state from xfs_buf_get() even on a
 * cache hit.
 *
 * IOWs, userspace is behaving quite differently to the kernel and as a result
 * it leaks errors from reads, invalidations and writes through
 * libxfs_getbuf/libxfs_readbuf.
 *
 * The result of this is that until the userspace code outside libxfs is cleaned
 * up, functions that release buffers from userspace control (i.e
 * libxfs_writebuf/libxfs_putbuf) need to zero bp->b_error to prevent
 * propagation of stale errors into future buffer operations.
 */

#define BDSTRAT_SIZE	(256 * 1024)

#define IO_BCOMPARE_CHECK
#ifdef XFS_BUF_TRACING

#undef libxfs_readbuf
#undef libxfs_readbuf_map
#undef libxfs_writebuf
#undef libxfs_getbuf
#undef libxfs_getbuf_map
#undef libxfs_getbuf_flags
#undef libxfs_putbuf

xfs_buf_t	*libxfs_readbuf(struct xfs_buftarg *, xfs_daddr_t, int, int,
				const struct xfs_buf_ops *);
xfs_buf_t	*libxfs_readbuf_map(struct xfs_buftarg *, struct xfs_buf_map *,
				int, int, const struct xfs_buf_ops *);
int		libxfs_writebuf(xfs_buf_t *, int);
xfs_buf_t	*libxfs_getbuf(struct xfs_buftarg *, xfs_daddr_t, int);
xfs_buf_t	*libxfs_getbuf_map(struct xfs_buftarg *, struct xfs_buf_map *,
				int, int);
xfs_buf_t	*libxfs_getbuf_flags(struct xfs_buftarg *, xfs_daddr_t, int,
				unsigned int);
void		libxfs_putbuf (xfs_buf_t *);

#define	__add_trace(bp, func, file, line)	\
do {						\
	if (bp) {				\
		(bp)->b_func = (func);		\
		(bp)->b_file = (file);		\
		(bp)->b_line = (line);		\
	}					\
} while (0)

xfs_buf_t *
libxfs_trace_readbuf(const char *func, const char *file, int line,
		struct xfs_buftarg *btp, xfs_daddr_t blkno, int len, int flags,
		const struct xfs_buf_ops *ops)
{
	xfs_buf_t	*bp = libxfs_readbuf(btp, blkno, len, flags, ops);
	__add_trace(bp, func, file, line);
	return bp;
}

xfs_buf_t *
libxfs_trace_readbuf_map(const char *func, const char *file, int line,
		struct xfs_buftarg *btp, struct xfs_buf_map *map, int nmaps, int flags,
		const struct xfs_buf_ops *ops)
{
	xfs_buf_t	*bp = libxfs_readbuf_map(btp, map, nmaps, flags, ops);
	__add_trace(bp, func, file, line);
	return bp;
}

int
libxfs_trace_writebuf(const char *func, const char *file, int line, xfs_buf_t *bp, int flags)
{
	__add_trace(bp, func, file, line);
	return libxfs_writebuf(bp, flags);
}

xfs_buf_t *
libxfs_trace_getbuf(const char *func, const char *file, int line,
		struct xfs_buftarg *btp, xfs_daddr_t blkno, int len)
{
	xfs_buf_t	*bp = libxfs_getbuf(btp, blkno, len);
	__add_trace(bp, func, file, line);
	return bp;
}

xfs_buf_t *
libxfs_trace_getbuf_map(const char *func, const char *file, int line,
		struct xfs_buftarg *btp, struct xfs_buf_map *map, int nmaps,
		int flags)
{
	xfs_buf_t	*bp = libxfs_getbuf_map(btp, map, nmaps, flags);
	__add_trace(bp, func, file, line);
	return bp;
}

xfs_buf_t *
libxfs_trace_getbuf_flags(const char *func, const char *file, int line,
		struct xfs_buftarg *btp, xfs_daddr_t blkno, int len, unsigned int flags)
{
	xfs_buf_t	*bp = libxfs_getbuf_flags(btp, blkno, len, flags);
	__add_trace(bp, func, file, line);
	return bp;
}

void
libxfs_trace_putbuf(const char *func, const char *file, int line, xfs_buf_t *bp)
{
	__add_trace(bp, func, file, line);
	libxfs_putbuf(bp);
}


#endif

kmem_zone_t			*xfs_buf_zone;

static struct cache_mru		xfs_buf_freelist =
	{{&xfs_buf_freelist.cm_list, &xfs_buf_freelist.cm_list},
	 0, PTHREAD_MUTEX_INITIALIZER };

/*
 * The bufkey is used to pass the new buffer information to the cache object
 * allocation routine. Because discontiguous buffers need to pass different
 * information, we need fields to pass that information. However, because the
 * blkno and bblen is needed for the initial cache entry lookup (i.e. for
 * bcompare) the fact that the map/nmaps is non-null to switch to discontiguous
 * buffer initialisation instead of a contiguous buffer.
 */
struct xfs_bufkey {
	struct xfs_buftarg	*buftarg;
	xfs_daddr_t		blkno;
	unsigned int		bblen;
	struct xfs_buf_map	*map;
	int			nmaps;
};

/*  2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
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

void
libxfs_bprint(xfs_buf_t *bp)
{
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

xfs_buf_t *
__libxfs_getbufr(int blen)
{
	xfs_buf_t	*bp;

	pthread_mutex_lock(&xfs_buf_freelist.cm_mutex);
	if (!list_empty(&xfs_buf_freelist.cm_list)) {
		list_for_each_entry(bp, &xfs_buf_freelist.cm_list, b_node.cn_mru) {
			if (bp->b_bcount == blen) {
				list_del_init(&bp->b_node.cn_mru);
				break;
			}
		}
		if (&bp->b_node.cn_mru == &xfs_buf_freelist.cm_list) {
			bp = list_entry(xfs_buf_freelist.cm_list.next,
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

xfs_buf_t *
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

#ifdef XFS_BUF_TRACING
struct list_head	lock_buf_list = {&lock_buf_list, &lock_buf_list};
int			lock_buf_count = 0;
#endif

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
#ifdef IO_DEBUG
		pthread_self(), __FUNCTION__,
		bp, bp->b_bn, (long long)LIBXFS_BBTOOFF64(key->blkno));
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

/*
 * Clean the buffer flags for libxfs_getbuf*(), which wants to return
 * an unused buffer with clean state.  This prevents CRC errors on a
 * re-read of a corrupt block that was prefetched and freed.  This
 * can happen with a massively corrupt directory that is discarded,
 * but whose blocks are then recycled into expanding lost+found.
 *
 * Note however that if the buffer's dirty (prefetch calls getbuf)
 * we'll leave the state alone because we don't want to discard blocks
 * that have been fixed.
 */
static void
reset_buf_state(
	struct xfs_buf	*bp)
{
	if (bp && !(bp->b_flags & LIBXFS_B_DIRTY))
		bp->b_flags &= ~(LIBXFS_B_UNCHECKED | LIBXFS_B_STALE |
				LIBXFS_B_UPTODATE);
}

struct xfs_buf *
libxfs_getbuf(struct xfs_buftarg *btp, xfs_daddr_t blkno, int len)
{
	struct xfs_buf	*bp;

	bp = libxfs_getbuf_flags(btp, blkno, len, 0);
	reset_buf_state(bp);
	return bp;
}

static struct xfs_buf *
__libxfs_getbuf_map(struct xfs_buftarg *btp, struct xfs_buf_map *map,
		    int nmaps, int flags)
{
	struct xfs_bufkey key = {0};
	int i;

	if (nmaps == 1)
		return libxfs_getbuf_flags(btp, map[0].bm_bn, map[0].bm_len,
					   flags);

	key.buftarg = btp;
	key.blkno = map[0].bm_bn;
	for (i = 0; i < nmaps; i++) {
		key.bblen += map[i].bm_len;
	}
	key.map = map;
	key.nmaps = nmaps;

	return __cache_lookup(&key, flags);
}

struct xfs_buf *
libxfs_getbuf_map(struct xfs_buftarg *btp, struct xfs_buf_map *map,
		  int nmaps, int flags)
{
	struct xfs_buf	*bp;

	bp = __libxfs_getbuf_map(btp, map, nmaps, flags);
	reset_buf_state(bp);
	return bp;
}

void
libxfs_putbuf(xfs_buf_t *bp)
{
	/*
	 * ensure that any errors on this use of the buffer don't carry
	 * over to the next user.
	 */
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

void
libxfs_purgebuf(xfs_buf_t *bp)
{
	struct xfs_bufkey key = {0};

	key.buftarg = bp->b_target;
	key.blkno = bp->b_bn;
	key.bblen = bp->b_length;

	cache_node_purge(libxfs_bcache, &key, (struct cache_node *)bp);
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
    g_print ("libxfs_readbufr = \r\n");
	int	fd = libxfs_device_to_fd(btp->dev);
	int	bytes = BBTOB(len);
	int	error;

	error = __read_buf(fd, bp->b_addr, bytes, LIBXFS_BBTOOFF64(blkno), flags);
	if (!error &&
	    bp->b_target->dev == btp->dev &&
	    bp->b_bn == blkno &&
	    bp->b_bcount == bytes)
		bp->b_flags |= LIBXFS_B_UPTODATE;
#ifdef IO_DEBUG
		pthread_self(), __FUNCTION__, bytes, error,
		(long long)LIBXFS_BBTOOFF64(blkno), (long long)blkno, bp);
#endif
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

	/*
	 * Set the ops on a cache miss (i.e. first physical read) as the
	 * verifier may change the ops to match the type of buffer it contains.
	 * A cache hit might reset the verifier to the original type if we set
	 * it again, but it won't get called again and set to match the buffer
	 * contents. *cough* xfs_da_node_buf_ops *cough*.
	 */
	error = libxfs_readbufr(btp, blkno, bp, len, flags);
	if (error)
		bp->b_error = error;
	else
		libxfs_readbuf_verify(bp, ops);
	return bp;
}

int
libxfs_readbufr_map(struct xfs_buftarg *btp, struct xfs_buf *bp, int flags)
{
	int	fd;
	int	error = 0;
	char	*buf;
	int	i;
	fd = libxfs_device_to_fd(btp->dev);
	buf = bp->b_addr;
	for (i = 0; i < bp->b_nmaps; i++) {
		off64_t	offset = LIBXFS_BBTOOFF64(bp->b_maps[i].bm_bn);
		int len = BBTOB(bp->b_maps[i].bm_len);

		error = __read_buf(fd, buf, len, offset, flags);
		if (error) {
			bp->b_error = error;
			break;
		}
		buf += len;
	}

	if (!error)
		bp->b_flags |= LIBXFS_B_UPTODATE;
#ifdef IO_DEBUG
		pthread_self(), __FUNCTION__, buf - (char *)bp->b_addr, error,
		(long long)LIBXFS_BBTOOFF64(bp->b_bn), (long long)bp->b_bn, bp);
#endif
	return error;
}

struct xfs_buf *
libxfs_readbuf_map(struct xfs_buftarg *btp, struct xfs_buf_map *map, int nmaps,
		int flags, const struct xfs_buf_ops *ops)
{
	struct xfs_buf	*bp;
	int		error = 0;

	if (nmaps == 1)
		return libxfs_readbuf(btp, map[0].bm_bn, map[0].bm_len,
					flags, ops);

	bp = __libxfs_getbuf_map(btp, map, nmaps, 0);
	if (!bp)
		return NULL;

	bp->b_error = 0;
	if ((bp->b_flags & (LIBXFS_B_UPTODATE|LIBXFS_B_DIRTY))) {
		if (bp->b_flags & LIBXFS_B_UNCHECKED)
			libxfs_readbuf_verify(bp, ops);
		return bp;
	}
	error = libxfs_readbufr_map(btp, bp, flags);
	if (!error)
		libxfs_readbuf_verify(bp, ops);

#ifdef IO_DEBUGX
		pthread_self(), __FUNCTION__, buf - (char *)bp->b_addr, error,
		(long long)LIBXFS_BBTOOFF64(bp->b_bn), (long long)bp->b_bn, bp);
#endif
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

#ifdef IO_DEBUG
			pthread_self(), __FUNCTION__, bp->b_bcount,
			(long long)LIBXFS_BBTOOFF64(bp->b_bn),
			(long long)bp->b_bn, bp, bp->b_error);
#endif
	if (!bp->b_error) {
		bp->b_flags |= LIBXFS_B_UPTODATE;
		bp->b_flags &= ~(LIBXFS_B_DIRTY | LIBXFS_B_EXIT |
				 LIBXFS_B_UNCHECKED);
	}
	return bp->b_error;
}

int
libxfs_writebuf_int(xfs_buf_t *bp, int flags)
{
	/*
	 * Clear any error hanging over from reading the buffer. This prevents
	 * subsequent reads after this write from seeing stale errors.
	 */
	bp->b_error = 0;
	bp->b_flags &= ~LIBXFS_B_STALE;
	bp->b_flags |= (LIBXFS_B_DIRTY | flags);
	return 0;
}

int
libxfs_writebuf(xfs_buf_t *bp, int flags)
{
#ifdef IO_DEBUG
			pthread_self(), __FUNCTION__,
			(long long)LIBXFS_BBTOOFF64(bp->b_bn),
			(long long)bp->b_bn);
#endif
	/*
	 * Clear any error hanging over from reading the buffer. This prevents
	 * subsequent reads after this write from seeing stale errors.
	 */
	bp->b_error = 0;
	bp->b_flags &= ~LIBXFS_B_STALE;
	bp->b_flags |= (LIBXFS_B_DIRTY | flags);
	libxfs_putbuf(bp);
	return 0;
}

void
libxfs_iomove(xfs_buf_t *bp, uint boff, int len, void *data, int flags)
{
#ifdef IO_DEBUG
	if (boff + len > bp->b_bcount) {
		abort();
	}
#endif
	switch (flags) {
	case LIBXFS_BZERO:
		memset(bp->b_addr + boff, 0, len);
		break;
	case LIBXFS_BREAD:
		memcpy(data, bp->b_addr + boff, len);
		break;
	case LIBXFS_BWRITE:
		memcpy(bp->b_addr + boff, data, len);
		break;
	}
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

	list_for_each_entry(bp, list, b_node.cn_mru) {
		if (bp->b_flags & LIBXFS_B_DIRTY)
		count++;
	}

	pthread_mutex_lock(&xfs_buf_freelist.cm_mutex);
	list_splice(list, &xfs_buf_freelist.cm_list);
	pthread_mutex_unlock(&xfs_buf_freelist.cm_mutex);

	return count;
}

/*
 * When a buffer is marked dirty, the error is cleared. Hence if we are trying
 * to flush a buffer prior to cache reclaim that has an error on it it means
 * we've already tried to flush it and it failed. Prevent repeated corruption
 * errors from being reported by skipping such buffers - when the corruption is
 * fixed the buffer will be marked dirty again and we can write it again.
 */
static int
libxfs_bflush(
	struct cache_node	*node)
{
	struct xfs_buf		*bp = (struct xfs_buf *)node;

	if (!bp->b_error && bp->b_flags & LIBXFS_B_DIRTY)
		return libxfs_writebufr(bp);
	return bp->b_error;
}

void
libxfs_putbufr(xfs_buf_t *bp)
{
	if (bp->b_flags & LIBXFS_B_DIRTY)
		libxfs_writebufr(bp);
	libxfs_brelse((struct cache_node *)bp);
}


void
libxfs_bcache_purge(void)
{
	cache_purge(libxfs_bcache);
}

void
libxfs_bcache_flush(void)
{
	cache_flush(libxfs_bcache);
}

int
libxfs_bcache_overflowed(void)
{
	return cache_overflowed(libxfs_bcache);
}

struct cache_operations libxfs_bcache_operations = {
	.hash		= libxfs_bhash,
	.alloc		= libxfs_balloc,
	.flush		= libxfs_bflush,
	.relse		= libxfs_brelse,
	.compare	= libxfs_bcompare,
	.bulkrelse	= libxfs_bulkrelse
};


/*
 * Inode cache stubs.
 */

extern kmem_zone_t	*xfs_ili_zone;
extern kmem_zone_t	*xfs_inode_zone;
