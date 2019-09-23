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
#define BTRFS_UUID_UNPARSED_SIZE	37
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

int btrfs_scan_lblkid(void);
int find_next_key(struct btrfs_path *path, struct btrfs_key *key);
u64 div_factor(u64 num, int factor);
void radix_tree_init(void);

#endif
