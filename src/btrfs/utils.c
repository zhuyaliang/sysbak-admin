/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 * Copyright (C) 2008 Morey Roof.  All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <fcntl.h>
#include <unistd.h>
#include <mntent.h>
#include <ctype.h>
#include <linux/loop.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <limits.h>
#include <blkid/blkid.h>
#include <sys/vfs.h>
#include <sys/statfs.h>
#include <linux/magic.h>
#include <getopt.h>
#include <btrfs/radix-tree.h>
//#include <btrfs/crc32c.h>
#include <btrfs/kerncompat.h>
#include <btrfs/ioctl.h>

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "utils.h"
#include "volumes.h"

#ifndef BLKDISCARD
#define BLKDISCARD	_IO(0x12,119)
#endif
#ifdef __KERNEL__
#define RADIX_TREE_MAP_SHIFT   (CONFIG_BASE_SMALL ? 4 : 6)
#else
#define RADIX_TREE_MAP_SHIFT   3       /* For more stressful testing */
#endif

#define RADIX_TREE_INDEX_BITS  (8 /* CHAR_BIT */ * sizeof(unsigned long))
#define RADIX_TREE_MAX_PATH (RADIX_TREE_INDEX_BITS/RADIX_TREE_MAP_SHIFT + 2)

static unsigned long height_to_maxindex[RADIX_TREE_MAX_PATH] __read_mostly;

static int btrfs_scan_done = 0;

static unsigned long __maxindex(unsigned int height)
{
    unsigned int tmp = height * RADIX_TREE_MAP_SHIFT;
    unsigned long index = ~0UL;

    if (tmp < RADIX_TREE_INDEX_BITS)
            index = (index >> (RADIX_TREE_INDEX_BITS - tmp - 1)) >> 1;
    return index;
}

void btrfs_radix_tree_init(void)
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(height_to_maxindex); i++)
               height_to_maxindex[i] = __maxindex(i);
}

char *__strncpy_null(char *dest, const char *src, size_t n)
{
	memcpy(dest, src, n);
	if (n > 0)
		dest[n - 1] = '\0';
	return dest;
}
int btrfs_scan_lblkid(void)
{
	int fd = -1;
	int ret;
	u64 num_devices;
	struct btrfs_fs_devices *tmp_devices;
	blkid_dev_iterate iter = NULL;
	blkid_dev dev = NULL;
	blkid_cache cache = NULL;
	char path[PATH_MAX];

	if (btrfs_scan_done)
		return 0;

	if (blkid_get_cache(&cache, NULL) < 0) {
		return 1;
	}
	blkid_probe_all(cache);
	iter = blkid_dev_iterate_begin(cache);
	blkid_dev_set_search(iter, "TYPE", "btrfs");
	while (blkid_dev_next(iter, &dev) == 0) {
		dev = blkid_verify(cache, dev);
		if (!dev)
			continue;
		strncpy_null(path, blkid_dev_devname(dev));

		fd = open(path, O_RDONLY);
		if (fd < 0) {
			continue;
		}
		ret = btrfs_scan_one_device(fd, path, &tmp_devices,
				&num_devices, BTRFS_SUPER_INFO_OFFSET, 0);
		if (ret) {
			close (fd);
			continue;
		}

		close(fd);
	}
	blkid_dev_iterate_end(iter);
	blkid_put_cache(cache);

	btrfs_scan_done = 1;

	return 0;
}

int find_next_key(struct btrfs_path *path, struct btrfs_key *key)
{
	int level;

	for (level = 0; level < BTRFS_MAX_LEVEL; level++) {
		if (!path->nodes[level])
			break;
		if (path->slots[level] + 1 >=
		    (int)btrfs_header_nritems(path->nodes[level]))
			continue;
		if (level == 0)
			btrfs_item_key_to_cpu(path->nodes[level], key,
					      path->slots[level] + 1);
		else
			btrfs_node_key_to_cpu(path->nodes[level], key,
					      path->slots[level] + 1);
		return 0;
	}
	return 1;
}
u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	num /= 10;
	return num;
}
