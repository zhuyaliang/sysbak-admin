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

#ifndef __GDBUS_FATFS_H__
#define __GDBUS_FATFS_H__

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <glib.h>
#include <gio/gio.h>
#include "sysbak-admin-generated.h"

#define FAT_12 1
#define FAT_16 2
#define FAT_32 3

// describers the FAT Boot sector, stolen from libparted
struct __attribute__ ((packed)) FatBootSector {
    uint8_t         boot_jump[3];   /* 00: Boot strap short or near jump */
    uint8_t         system_id[8];   /* 03: system name */
    uint16_t        sector_size;    /* 0b: bytes per logical sector */
    uint8_t         cluster_size;   /* 0d: sectors/cluster */
    uint16_t        reserved;       /* 0e: reserved sectors */
    uint8_t         fats;           /* 10: number of FATs */
    uint16_t        dir_entries;    /* 11: number of root directory entries */
    uint16_t        sectors;        /* 13: if 0, total_sect supersedes */
    uint8_t         media;          /* 15: media code */
    uint16_t        fat_length;     /* 16: sectors/FAT for FAT12/16 */
    uint16_t        secs_track;     /* 18: sectors per track */
    uint16_t        heads;          /* 1a: number of heads */
    uint32_t        hidden;         /* 1c: hidden sectors (partition start) */
    uint32_t        sector_count;   /* 20: no. of sectors (if sectors == 0) */

    union __attribute__ ((packed)) {
        /* FAT16 fields */
        struct __attribute__ ((packed)) {
        uint8_t         drive_num;      /* 24: */
        uint8_t         empty_1;        /* 25: */
        uint8_t         ext_signature;  /* 26: always 0x29 */
        uint32_t        serial_number;  /* 27: */
        uint8_t         volume_name [11];       /* 2b: */
        uint8_t         fat_name [8];   /* 36: */
        uint8_t         boot_code[448]; /* 3f: Boot code (or message) */
        } fat16;
        /* FAT32 fields */
        struct __attribute__ ((packed)) {
        uint32_t        fat_length;     /* 24: size of FAT in sectors */
        uint16_t        flags;          /* 28: bit8: fat mirroring, low4: active fat */
        uint16_t        version;        /* 2a: minor * 256 + major */
        uint32_t        root_dir_cluster;       /* 2c: */
        uint16_t        info_sector;    /* 30: */
        uint16_t        backup_sector;  /* 32: */
        uint8_t         empty_1 [12];   /* 34: */
        uint16_t        drive_num;      /* 40: */
        uint8_t         ext_signature;  /* 42: always 0x29 */
        uint32_t        serial_number;  /* 43: */
        uint8_t         volume_name [11];       /* 47: */
        uint8_t         fat_name [8];   /* 52: */
        uint8_t         boot_code[420]; /* 5a: Boot code (or message) */
        } fat32;
    } u;

    uint16_t        boot_sign;      /* 1fe: always 0xAA55 */
};
typedef struct FatBootSector FatBootSector;

struct FatFsInfo{
    uint32_t magic;
    uint8_t  rev[480];
    uint32_t signature;
    uint32_t free_count;
    uint32_t next_free;
    uint32_t reserved2[3];
    uint32_t trail;
};

typedef struct FatFsInfo FatFsInfo;

gboolean      gdbus_sysbak_fatfs_ptf      (SysbakGdbus           *object,
                                           GDBusMethodInvocation *invocation,
                                           const gchar           *source,
                                           const gchar           *target,
                                           gboolean               overwrite);

gboolean      gdbus_sysbak_fatfs_ptp      (SysbakGdbus           *object,
                                           GDBusMethodInvocation *invocation,
                                           const gchar           *source,
                                           const gchar           *target,
                                           gboolean               overwrite);

#endif
