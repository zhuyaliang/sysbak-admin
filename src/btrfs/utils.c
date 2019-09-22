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
#include <btrfs/crc32c.h>
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

static int btrfs_scan_done = 0;

const char *pretty_size_mode(u64 size, unsigned mode)
{
	static __thread int ps_index = 0;
	static __thread char ps_array[10][32];
	char *ret;

	ret = ps_array[ps_index];
	ps_index++;
	ps_index %= 10;
	(void)pretty_size_snprintf(size, ret, 32, mode);

	return ret;
}

static const char* unit_suffix_binary[] =
	{ "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
static const char* unit_suffix_decimal[] =
	{ "B", "kB", "MB", "GB", "TB", "PB", "EB"};

int pretty_size_snprintf(u64 size, char *str, size_t str_size, unsigned unit_mode)
{
	int num_divs;
	float fraction;
	u64 base = 0;
	int mult = 0;
	const char** suffix = NULL;
	u64 last_size;

	if (str_size == 0)
		return 0;

	if ((unit_mode & ~UNITS_MODE_MASK) == UNITS_RAW) {
		snprintf(str, str_size, "%llu", size);
		return 0;
	}

	if ((unit_mode & ~UNITS_MODE_MASK) == UNITS_BINARY) {
		base = 1024;
		mult = 1024;
		suffix = unit_suffix_binary;
	} else if ((unit_mode & ~UNITS_MODE_MASK) == UNITS_DECIMAL) {
		base = 1000;
		mult = 1000;
		suffix = unit_suffix_decimal;
	}

	if (!base) {
		fprintf(stderr, "INTERNAL ERROR: unknown unit base, mode %u\n",
				unit_mode);
		assert(0);
		return -1;
	}

	num_divs = 0;
	last_size = size;
	switch (unit_mode & UNITS_MODE_MASK) {
	case UNITS_TBYTES: base *= mult; num_divs++;
	case UNITS_GBYTES: base *= mult; num_divs++;
	case UNITS_MBYTES: base *= mult; num_divs++;
	case UNITS_KBYTES: num_divs++;
			   break;
	case UNITS_BYTES:
			   base = 1;
			   num_divs = 0;
			   break;
	default:
		while (size >= mult) {
			last_size = size;
			size /= mult;
			num_divs++;
		}
		if (num_divs == 0)
			base = 1;
	}

	if (num_divs >= ARRAY_SIZE(unit_suffix_binary)) {
		str[0] = '\0';
		printf("INTERNAL ERROR: unsupported unit suffix, index %d\n",
				num_divs);
		assert(0);
		return -1;
	}
	fraction = (float)last_size / base;

	return snprintf(str, str_size, "%.2f%s", fraction, suffix[num_divs]);
}

char *__strncpy_null(char *dest, const char *src, size_t n)
{
	strncpy(dest, src, n);
	if (n > 0)
		dest[n - 1] = '\0';
	return dest;
}
u64 parse_qgroupid(const char *p)
{
	char *s = strchr(p, '/');
	const char *ptr_src_end = p + strlen(p);
	char *ptr_parse_end = NULL;
	u64 level;
	u64 id;
	int fd;
	int ret = 0;

	if (p[0] == '/')
		goto path;

	if (!s) {
		id = strtoull(p, &ptr_parse_end, 10);
		if (ptr_parse_end != ptr_src_end)
			goto path;
		return id;
	}
	level = strtoull(p, &ptr_parse_end, 10);
	if (ptr_parse_end != s)
		goto path;

	id = strtoull(s + 1, &ptr_parse_end, 10);
	if (ptr_parse_end != ptr_src_end)
		goto  path;

	return (level << BTRFS_QGROUP_LEVEL_SHIFT) | id;

path:
	ret = test_issubvolume(p);
	if (ret < 0 || !ret)
		goto err;
	fd = open(p, O_RDONLY);
	if (fd < 0)
		goto err;
	ret = lookup_ino_rootid(fd, &id);
	close(fd);
	if (ret < 0)
		goto err;
	return id;

err:
	error("invalid qgroupid or subvolume path: %s", p);
	exit(-1);
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
		error("blkid cache get failed");
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
			error("cannot open %s: %s", path, strerror(errno));
			continue;
		}
		ret = btrfs_scan_one_device(fd, path, &tmp_devices,
				&num_devices, BTRFS_SUPER_INFO_OFFSET, 0);
		if (ret) {
			error("cannot scan %s: %s", path, strerror(-ret));
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
int lookup_ino_rootid(int fd, u64 *rootid)
{
	struct btrfs_ioctl_ino_lookup_args args;
	int ret;

	memset(&args, 0, sizeof(args));
	args.treeid = 0;
	args.objectid = BTRFS_FIRST_FREE_OBJECTID;

	ret = ioctl(fd, BTRFS_IOC_INO_LOOKUP, &args);
	if (ret < 0) {
		error("failed to lookup root id: %s", strerror(errno));
		return ret;
	}

	*rootid = args.treeid;

	return 0;
}


int find_next_key(struct btrfs_path *path, struct btrfs_key *key)
{
	int level;

	for (level = 0; level < BTRFS_MAX_LEVEL; level++) {
		if (!path->nodes[level])
			break;
		if (path->slots[level] + 1 >=
		    btrfs_header_nritems(path->nodes[level]))
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
int test_issubvolume(const char *path)
{
    struct stat st;
    struct statfs stfs;
    int     res;

    res = stat(path, &st);
    if (res < 0)
        return -errno;

    if (st.st_ino != BTRFS_FIRST_FREE_OBJECTID || !S_ISDIR(st.st_mode))
        return 0;

    res = statfs(path, &stfs);
    if (res < 0)
        return -errno;

    return (int)stfs.f_type == BTRFS_SUPER_MAGIC;
}

u64 arg_strtou64(const char *str)
{
	u64 value;
	char *ptr_parse_end = NULL;

	value = strtoull(str, &ptr_parse_end, 0);
	if (ptr_parse_end && *ptr_parse_end != '\0') {
		fprintf(stderr, "ERROR: %s is not a valid numeric value.\n",
			str);
		exit(1);
	}

	if (str[0] == '-') {
		fprintf(stderr, "ERROR: %s: negative value is invalid.\n",
			str);
		exit(1);
	}
	if (value == ULLONG_MAX) {
		fprintf(stderr, "ERROR: %s is too large.\n", str);
		exit(1);
	}
	return value;
}
