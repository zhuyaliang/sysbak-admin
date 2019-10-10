/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_BIT_H__
#define	__XFS_BIT_H__

/*
 * XFS bit manipulation routines.
 */

/*
 * masks with n high/low bits set, 64-bit values
 */
static inline __uint64_t xfs_mask64hi(int n)
{
	return (__uint64_t)-1 << (64 - (n));
}
static inline __uint32_t xfs_mask32lo(int n)
{
	return ((__uint32_t)1 << (n)) - 1;
}
static inline __uint64_t xfs_mask64lo(int n)
{
	return ((__uint64_t)1 << (n)) - 1;
}

static inline int fls(int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		r -= 1;
	}
	return r;
}
static inline int fls64(__u64 x)
{
	__u32 h = x >> 32;
	if (h)
		return fls(h) + 32;
	return fls(x);
}
/* Get high bit set out of 32-bit argument, -1 if none set */
static inline int xfs_highbit32(__uint32_t v)
{
	return fls(v) - 1;
}

/* Get high bit set out of 64-bit argument, -1 if none set */
static inline int xfs_highbit64(__uint64_t v)
{
	return fls64(v) - 1;
}

/* Get low bit set out of 32-bit argument, -1 if none set */
static inline int xfs_lowbit32(__uint32_t v)
{
	return ffs(v) - 1;
}

/* Get low bit set out of 64-bit argument, -1 if none set */
static inline int xfs_lowbit64(__uint64_t v)
{
	__uint32_t	w = (__uint32_t)v;
	int		n = 0;

	if (w) {	/* lower bits */
		n = ffs(w);
	} else {	/* upper bits */
		w = (__uint32_t)(v >> 32);
		if (w) {
			n = ffs(w);
			if (n)
				n += 32;
		}
	}
	return n - 1;
}

#endif	/* __XFS_BIT_H__ */
