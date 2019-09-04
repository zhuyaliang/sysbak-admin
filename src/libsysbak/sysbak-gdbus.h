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

#ifndef __SYSBAK_GDBUS_H__
#define __SYSBAK_GDBUS_H__

#include <glib.h>
#include <gio/gio.h>
#include "sysbak-share.h"

device_info *get_extfs_device_info (const char *dev_name);
gboolean gdbus_sysbak_extfs_ptf (const char *source,
		                         const char *target,
								 int         dfr,
								 int         dfw,
								 GError    **error);
gboolean gdbus_sysbak_extfs_ptp (const char *source,
		                         const char *target,
								 int         dfr,
								 int         dfw,
								 GError    **error);
#endif
