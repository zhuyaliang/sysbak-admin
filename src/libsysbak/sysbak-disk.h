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

#ifndef __SYSBAK_DISK_H__
#define __SYSBAK_DISK_H__

#include <glib.h>
#include <gio/gio.h>
#include "sysbak-gdbus.h"

gboolean       get_disk_info_config        (const char  *disk_name,
                                            const char  *config_name);

gboolean       get_disk_mbr                (SysbakAdmin *sysbak);

gboolean       get_disk_partition_table    (SysbakAdmin *sysbak);

gboolean       set_disk_partition_table    (SysbakAdmin *sysbak);

gboolean       get_disk_lvm_metadata       (SysbakAdmin *sysbak);

gboolean       set_disk_lvm_metadata       (SysbakAdmin *sysbak);

gboolean       sysbak_admin_disk_to_file   (SysbakAdmin *sysbak);

gboolean       create_lvm_pv               (SysbakAdmin *sysbak,
                                            const char  *uuid);

gboolean       restart_lvm_vg              (SysbakAdmin *sysbak);

#endif
