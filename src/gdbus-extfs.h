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

#ifndef __GDBUS_EXTFS_H__
#define __GDBUS_EXTFS_H__

#include <glib.h>
#include <gio/gio.h>
#include <io-generated.h>


gboolean      gdbus_open_file             (IoGdbus               *object,
                                           GDBusMethodInvocation *invocation,
                                           const gchar           *filename,
                                           guint                  flag,
					                       guint                  mode);

gboolean      gdbus_sysbak_extfs_ptf      (IoGdbus               *object,
                                           GDBusMethodInvocation *invocation,
										   gint                   dfr,
										   gint                   dfw,
										   const gchar           *source,
										   const gchar           *target);

gboolean      gdbus_sysbak_extfs_ptp      (IoGdbus               *object,
                                           GDBusMethodInvocation *invocation,
										   gint                   dfr,
										   gint                   dfw,
										   const gchar           *source,
										   const gchar           *target);

gboolean      gdbus_sysbak_restore        (IoGdbus               *object,
                                           GDBusMethodInvocation *invocation,
										   gint                   dfr,
										   gint                   dfw,
										   const gchar           *source,
										   const gchar           *target);

gboolean      gdbus_get_extfs_device_info (IoGdbus               *object,
		                                   GDBusMethodInvocation *invocation,
									       const char            *device);
#endif
