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

#ifndef __BTRFS_UTILS_H__
#define __BTRFS_UTILS_H__

#include <sys/stat.h>
#include "ctree.h"
#include <dirent.h>
#include <stdarg.h>

#define BTRFS_MKFS_SYSTEM_GROUP_SIZE (4 * 1024 * 1024)
#define BTRFS_MKFS_SMALL_VOLUME_SIZE (1024 * 1024 * 1024)
#define BTRFS_MKFS_DEFAULT_NODE_SIZE 16384
#define BTRFS_MKFS_DEFAULT_FEATURES 				\
		(BTRFS_FEATURE_INCOMPAT_EXTENDED_IREF		\
		| BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA)

/*
 * Avoid multi-device features (RAID56) and mixed block groups
 */

#define BTRFS_CONVERT_META_GROUP_SIZE (32 * 1024 * 1024)

#define BTRFS_FEATURE_LIST_ALL		(1ULL << 63)

#define BTRFS_SCAN_MOUNTED	(1ULL << 0)
#define BTRFS_SCAN_LBLKID	(1ULL << 1)

#define BTRFS_UPDATE_KERNEL	1

#define BTRFS_ARG_UNKNOWN	0
#define BTRFS_ARG_MNTPOINT	1
#define BTRFS_ARG_UUID		2
#define BTRFS_ARG_BLKDEV	3
#define BTRFS_ARG_REG		4

#define BTRFS_UUID_UNPARSED_SIZE	37

#define ARGV0_BUF_SIZE	PATH_MAX

#define GETOPT_VAL_SI				256
#define GETOPT_VAL_IEC				257
#define GETOPT_VAL_RAW				258
#define GETOPT_VAL_HUMAN_READABLE		259
#define GETOPT_VAL_KBYTES			260
#define GETOPT_VAL_MBYTES			261
#define GETOPT_VAL_GBYTES			262
#define GETOPT_VAL_TBYTES			263

#define GETOPT_VAL_HELP				270

/*
 * Output modes of size
 */
#define UNITS_RESERVED			(0)
#define UNITS_BYTES			(1)
#define UNITS_KBYTES			(2)
#define UNITS_MBYTES			(3)
#define UNITS_GBYTES			(4)
#define UNITS_TBYTES			(5)
#define UNITS_RAW			(1U << UNITS_MODE_SHIFT)
#define UNITS_BINARY			(2U << UNITS_MODE_SHIFT)
#define UNITS_DECIMAL			(3U << UNITS_MODE_SHIFT)
#define UNITS_MODE_MASK			((1U << UNITS_MODE_SHIFT) - 1)
#define UNITS_MODE_SHIFT		(8)
#define UNITS_HUMAN_BINARY		(UNITS_BINARY)
#define UNITS_HUMAN_DECIMAL		(UNITS_DECIMAL)
#define UNITS_HUMAN			(UNITS_HUMAN_BINARY)
#define UNITS_DEFAULT			(UNITS_HUMAN)

#define min(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })

#define min_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#define max_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })


char *__strncpy_null(char *dest, const char *src, size_t n);
#define strncpy_null(dest, src) __strncpy_null(dest, src, sizeof(dest))

const char *pretty_size_mode(u64 size, unsigned mode);
int pretty_size_snprintf(u64 size, char *str, size_t str_size, unsigned unit_mode);
u64 parse_qgroupid(const char *p);
int btrfs_scan_lblkid(void);
int lookup_ino_rootid(int fd, u64 *rootid);
int find_next_key(struct btrfs_path *path, struct btrfs_key *key);
int test_issubvolume(const char *path);
u64 arg_strtou64(const char *str);
static inline u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	num /= 10;
	return num;
}

__attribute__ ((format (printf, 1, 2)))
static inline void warning(const char *fmt, ...)
{
    va_list args;

    fputs("WARNING: ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

__attribute__ ((format (printf, 2, 3)))
static inline int warning_on(int condition, const char *fmt, ...)
{
    va_list args;

    if (!condition)
        return 0;

    fputs("WARNING: ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);

    return 1;
}

#endif
