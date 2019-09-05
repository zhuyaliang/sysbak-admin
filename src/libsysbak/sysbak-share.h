/*  sysbak-admin 
*   Copyright (C) 2019  zhuyaliang https://github.com/zhuyaliang/
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.

*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.

*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef __SYSBAK_SHARE_H__
#define __SYSBAK_SHARE_H__


#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#define     FS_MAGIC_SIZE 15
#define     extfs_MAGIC "EXTFS"
#define     ext2_MAGIC "EXT2"
#define     ext3_MAGIC "EXT3"
#define     ext4_MAGIC "EXT4"

typedef struct
{
	char fs[FS_MAGIC_SIZE+1];
    unsigned long long totalblock;
    unsigned long long usedblocks;
    unsigned int  block_size;
}device_info;


typedef struct
{
	float  percent;
    float  speed;
    time_t remained;
    time_t elapsed;
}progress_data;


typedef unsigned long long ull;
typedef unsigned int       uint;
typedef void (* sysbak_progress) (progress_data*,gpointer);


#endif
