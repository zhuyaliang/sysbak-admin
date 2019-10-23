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

#ifndef __GDBUS_DISK_H__
#define __GDBUS_DISK_H__

#include <glib.h>
#include <gio/gio.h>
#include "sysbak-admin-generated.h"

gboolean    gdbus_create_pv              (SysbakGdbus           *object,
                                          GDBusMethodInvocation *invocation,
						                  const gchar           *file,
                                          const gchar           *uuid,
                                          const gchar           *device);

gboolean    gdbus_restart_vg             (SysbakGdbus           *object,
                                          GDBusMethodInvocation *invocation,
						                  const gchar           *vgname);

gboolean    gdbus_backup_partition_table (SysbakGdbus            *object,
                                          GDBusMethodInvocation  *invocation,
								          const gchar            *source,
								          const gchar            *target);

gboolean    gdbus_restore_partition_table (SysbakGdbus           *object,
                                           GDBusMethodInvocation *invocation,
				 				           const gchar           *source,
								           const gchar           *target);

gboolean    gdbus_backup_disk_mbr        (SysbakGdbus            *object,
                                          GDBusMethodInvocation  *invocation,
								          const gchar            *source,
								          const gchar            *target);

gboolean    gdbus_backup_lvm_meta        (SysbakGdbus            *object,
                                          GDBusMethodInvocation  *invocation,
							              const gchar            *target);

gboolean    gdbus_restore_lvm_meta       (SysbakGdbus           *object,
                                          GDBusMethodInvocation *invocation,
					 		              const gchar           *file,
                                          const gchar           *vgname);
#endif
