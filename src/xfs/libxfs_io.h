/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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

#ifndef __LIBXFS_IO_H_
#define __LIBXFS_IO_H_

#include "cache.h"
/*
 * Kernel equivalent buffer based I/O interface
 */

struct xfs_buf;
struct xfs_mount;
struct xfs_perag;

/*
 * IO verifier callbacks need the xfs_mount pointer, so we have to behave
 * somewhat like the kernel now for userspace IO in terms of having buftarg
 * based devices...
 */
struct xfs_buftarg {
	struct xfs_mount	*bt_mount;
	dev_t			dev;
};
enum xfs_buf_flags_t {  /* b_flags bits */
    LIBXFS_B_EXIT       = 0x0001,   /* ==LIBXFS_EXIT_ON_FAILURE */
    LIBXFS_B_DIRTY      = 0x0002,   /* buffer has been modified */
    LIBXFS_B_STALE      = 0x0004,   /* buffer marked as invalid */
    LIBXFS_B_UPTODATE   = 0x0008,   /* buffer is sync'd to disk */
    LIBXFS_B_DISCONTIG  = 0x0010,   /* discontiguous buffer */
    LIBXFS_B_UNCHECKED  = 0x0020,   /* needs verification */
};

#define LIBXFS_BBTOOFF64(bbs)	(((xfs_off_t)(bbs)) << BBSHIFT)

#define XB_PAGES        2
struct xfs_buf_map {
	xfs_daddr_t		bm_bn;  /* block number for I/O */
	int			bm_len; /* size of I/O */
};

struct xfs_buf_ops {
	char *name;
	void (*verify_read)(struct xfs_buf *);
	void (*verify_write)(struct xfs_buf *);
};

typedef struct xfs_buf {
	struct cache_node	b_node;
	unsigned int		b_flags;
	xfs_daddr_t		b_bn;
	unsigned		b_bcount;
	unsigned int		b_length;
	struct xfs_buftarg	*b_target;
#define b_dev		b_target->dev
	pthread_mutex_t		b_lock;
	pthread_t		b_holder;
	unsigned int		b_recur;
	void			*b_fspriv;
	void			*b_fsprivate2;
	void			*b_fsprivate3;
	void			*b_addr;
	int			b_error;
	const struct xfs_buf_ops *b_ops;
	struct xfs_perag	*b_pag;
	struct xfs_buf_map	*b_maps;
	int			b_nmaps;
#ifdef XFS_BUF_TRACING
	struct list_head	b_lock_list;
	const char		*b_func;
	const char		*b_file;
	int			b_line;
#endif
} xfs_buf_t;

extern struct cache	*libxfs_bcache;
extern struct cache_operations	libxfs_bcache_operations;

#define LIBXFS_GETBUF_TRYLOCK	(1 << 0)

extern xfs_buf_t *libxfs_readbuf(struct xfs_buftarg *, xfs_daddr_t, int, int,
			const struct xfs_buf_ops *);
extern xfs_buf_t *libxfs_readbuf_map(struct xfs_buftarg *, struct xfs_buf_map *,
			int, int, const struct xfs_buf_ops *);
extern int	libxfs_writebuf(xfs_buf_t *, int);
extern xfs_buf_t *libxfs_getbuf(struct xfs_buftarg *, xfs_daddr_t, int);
extern xfs_buf_t *libxfs_getbuf_map(struct xfs_buftarg *,
			struct xfs_buf_map *, int, int);
extern xfs_buf_t *libxfs_getbuf_flags(struct xfs_buftarg *, xfs_daddr_t,
			int, unsigned int);
extern void	libxfs_putbuf (xfs_buf_t *);

extern void	libxfs_readbuf_verify(struct xfs_buf *bp,
			const struct xfs_buf_ops *ops);
extern xfs_buf_t *libxfs_getbufr(struct xfs_buftarg *, xfs_daddr_t, int);
extern void	libxfs_putbufr(xfs_buf_t *);
extern int	libxfs_writebuf_int(xfs_buf_t *, int);
extern int	libxfs_writebufr(struct xfs_buf *);
extern int	libxfs_readbufr(struct xfs_buftarg *, xfs_daddr_t, xfs_buf_t *, int, int);
extern int	libxfs_readbufr_map(struct xfs_buftarg *, struct xfs_buf *, int);
extern int libxfs_bhash_size;

#define LIBXFS_BREAD	0x1
#define LIBXFS_BWRITE	0x2
#define LIBXFS_BZERO	0x4

#endif	/* __LIBXFS_IO_H__ */
