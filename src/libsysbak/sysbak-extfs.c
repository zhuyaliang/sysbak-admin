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

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ext2fs/ext2fs.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>

#include "sysbak-extfs.h"
#include "sysbak-admin-generated.h"

//Backup partition to image file 
gboolean sysbak_admin_extfs_ptf_async (SysbakAdmin *sysbak)
{
	const char  *source,*target;
	gboolean     overwrite;
	SysbakGdbus *proxy;
	g_autoptr(GError) error = NULL;
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
	
	source = sysbak_admin_get_source (sysbak);
	target = sysbak_admin_get_target (sysbak);
	overwrite = sysbak_admin_get_option (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
    
	if (!sysbak_gdbus_call_sysbak_extfs_ptf_sync (proxy,
                                              source,
                                              target,
                                              overwrite,
											  NULL,
											 &error))
	{
		g_warning ("libgdbus_sysbak_extfs_ptf failed %s\r\n",error->message);
		return FALSE;
	}

    return TRUE;      /// finish
}
gboolean sysbak_admin_extfs_ptp_async (SysbakAdmin *sysbak)
{
	const char  *source,*target;
	gboolean     overwrite;
	SysbakGdbus *proxy;
	g_autoptr(GError) error = NULL;
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
	
	source = sysbak_admin_get_source (sysbak);
	target = sysbak_admin_get_target (sysbak);
	overwrite = sysbak_admin_get_option (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

	if (!sysbak_gdbus_call_sysbak_extfs_ptp_sync (proxy,
											  source,
											  target,
                                              overwrite,
											  NULL,
											 &error))
    {
		g_warning ("libgdbus_sysbak_extfs_ptp failed %s\r\n",error->message);
		return FALSE;
    } 

    return TRUE;      /// finish
}
gboolean sysbak_admin_extfs_restore_async (SysbakAdmin *sysbak)
{
	const char  *source,*target;
	gboolean     overwrite;
	SysbakGdbus *proxy;
	g_autoptr(GError) error = NULL;
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
	
	source = sysbak_admin_get_source (sysbak);
	target = sysbak_admin_get_target (sysbak);
	overwrite = sysbak_admin_get_option (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

	if (!sysbak_gdbus_call_sysbak_restore_sync (proxy,
				                            source,
										    target,
                                            overwrite,
											NULL,
										   &error))
    {
		g_warning ("libgdbus_sysbak_extfs_restore failed %s\r\n",error->message);
		return FALSE;
    } 

    return TRUE;      /// finish
}
