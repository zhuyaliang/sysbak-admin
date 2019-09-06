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
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include <mntent.h>
#include <glib.h>

#include "sysbak-extfs.h"
#include "sysbak-admin-generated.h"

static gboolean check_file_device (const char *path,GError **error)
{
    if (access (path,F_OK) == -1)
    {
        g_set_error_literal (error, 
                             G_IO_ERROR, 
                             G_IO_ERROR_FAILED,
			                "file does not exist");
        return FALSE;
    }    
    
    return TRUE;
}    
//Backup partition to image file 
gboolean sysbak_admin_extfs_ptf_async (SysbakAdmin *sysbak,GError **error)
{
	const char  *source,*target;
	gboolean     overwrite;
	SysbakGdbus *proxy;
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
	
	source = sysbak_admin_get_source (sysbak);
	target = sysbak_admin_get_target (sysbak);
	overwrite = sysbak_admin_get_option (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
    
    if (!check_file_device (source,error))
    {
        return FALSE;
    }  
    if(check_file_device (target,error) && overwrite)
    {
        return FALSE;
    }       
    if (check_device_mount (source))
    {
        g_set_error_literal (error, 
                             G_IO_ERROR, 
                             G_IO_ERROR_FAILED,
			                "source device has been mounted");
        return FALSE;
    }   
    error = NULL;
	if (!sysbak_gdbus_call_sysbak_extfs_ptf_sync (proxy,
                                                  source,
                                                  target,
                                                  overwrite,
											      NULL,
											      error))
	{
		return FALSE;
	}

    return TRUE;      /// finish
}
gboolean sysbak_admin_extfs_ptp_async (SysbakAdmin *sysbak,GError **error)
{
	const char  *source,*target;
	gboolean     overwrite;
	SysbakGdbus *proxy;
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
	
	source = sysbak_admin_get_source (sysbak);
	target = sysbak_admin_get_target (sysbak);
	overwrite = sysbak_admin_get_option (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (source,error))
    {
        return FALSE;
    }   
    if (!check_file_device (target,error))
    {
        return FALSE;
    }    
    if (check_device_mount (source))
    {
        g_set_error_literal (error, 
                             G_IO_ERROR, 
                             G_IO_ERROR_FAILED,
			                "source device has been mounted");
        return FALSE;
    }    
    if (check_device_mount (target))
    {
        g_set_error_literal (error, 
                             G_IO_ERROR, 
                             G_IO_ERROR_FAILED,
			                "target device has been mounted");
        return FALSE;
    }    

	if (!sysbak_gdbus_call_sysbak_extfs_ptp_sync (proxy,
											      source,
											      target,
                                                  overwrite,
											      NULL,
											      error))
    {
		return FALSE;
    } 

    return TRUE;      /// finish
}
gboolean sysbak_admin_extfs_restore_async (SysbakAdmin *sysbak,GError **error)
{
	const char  *source,*target;
	gboolean     overwrite;
	SysbakGdbus *proxy;
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
	
	source = sysbak_admin_get_source (sysbak);
	target = sysbak_admin_get_target (sysbak);
	overwrite = sysbak_admin_get_option (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
    
    if (!check_file_device (source,error))
    {
        return FALSE;
    }   
    if (!check_file_device (target,error))
    {
        return FALSE;
    }   

    if (check_device_mount (target))
    {
        g_set_error_literal (error, 
                             G_IO_ERROR, 
                             G_IO_ERROR_FAILED,
			                "target device has been mounted");
        return FALSE;
    }    
	if (!sysbak_gdbus_call_sysbak_restore_sync (proxy,
				                                source,
										        target,
                                                overwrite,
											    NULL,
										        error))
    {
		return FALSE;
    } 

    return TRUE;      /// finish
}
