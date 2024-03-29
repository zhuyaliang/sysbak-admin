/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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

#include <mntent.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/sysinfo.h>

#include "libxfs.h"
#include "xfs_fs.h"

int platform_has_uuid = 1;
static int max_block_alignment;

#ifndef BLKGETSIZE64
# define BLKGETSIZE64	_IOR(0x12,114,size_t)
#endif
#ifndef BLKBSZSET
# define BLKBSZSET	_IOW(0x12,113,size_t)
#endif
#ifndef BLKSSZGET
# define BLKSSZGET	_IO(0x12,104)
#endif

#ifndef RAMDISK_MAJOR
#define RAMDISK_MAJOR	1	/* ramdisk major number */
#endif

/*
 * Check if the filesystem is mounted.  Be verbose if asked, and
 * optionally restrict check to /writable/ mounts (i.e. RO is OK)
 */
#define	CHECK_MOUNT_VERBOSE	0x1
#define	CHECK_MOUNT_WRITABLE	0x2

int
platform_set_blocksize(int fd, char *path, dev_t device, int blocksize, int fatal)
{
	int error = 0;

	if (major(device) != RAMDISK_MAJOR) 
    {
	    error = ioctl(fd, BLKBSZSET, &blocksize);
        
	}
	return error;
}

void
platform_flush_device(int fd, dev_t device)
{
	struct stat	st;
	if (major(device) == RAMDISK_MAJOR)
		return;

	if (fstat(fd, &st) < 0)
		return;

	if (S_ISREG(st.st_mode))
		fsync(fd);
	else
		ioctl(fd, BLKFLSBUF, 0);
}

void
platform_findsizes(char *path, int fd, long long *sz, int *bsz)
{
	struct stat	st;
	__uint64_t	size;
	int		error;

    fstat(fd, &st);
	if ((st.st_mode & S_IFMT) == S_IFREG) {
		struct dioattr	da;

		*sz = (long long)(st.st_size >> 9);

		if (ioctl(fd, XFS_IOC_DIOINFO, &da) < 0) {
			/*
			 * fall back to BBSIZE; mkfs might fail if there's a
			 * size mismatch between the image & the host fs...
			 */
			*bsz = BBSIZE;
		} else
			*bsz = da.d_miniosz;

		if (*bsz > max_block_alignment)
			max_block_alignment = *bsz;
		return;
	}

	error = ioctl(fd, BLKGETSIZE64, &size);
	if (error >= 0) {
		/* BLKGETSIZE64 returns size in bytes not 512-byte blocks */
		*sz = (long long)(size >> 9);
	} else {
		/* If BLKGETSIZE64 fails, try BLKGETSIZE */
		unsigned long tmpsize;

		error = ioctl(fd, BLKGETSIZE, &tmpsize);
		*sz = (long long)tmpsize;
	}

	if (ioctl(fd, BLKSSZGET, bsz) < 0) {
		*bsz = BBSIZE;
	}
	if (*bsz > max_block_alignment)
		max_block_alignment = *bsz;
}

char *
platform_findrawpath(char *path)
{
	return path;
}

char *
platform_findblockpath(char *path)
{
	return path;
}

int
platform_direct_blockdev(void)
{
	return 1;
}

int
platform_align_blockdev(void)
{
	if (!max_block_alignment)
		return getpagesize();
	return max_block_alignment;
}
