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

static void call_sysbak_extfs_ptf (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      data)
{
	SysbakAdmin *sysbak = SYSBAK_ADMIN (data);
	SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
	g_autofree gchar *error_message = NULL;
	const char  *base_error = "Backup partition to file failed";
	
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
	if (! sysbak_gdbus_call_sysbak_extfs_ptf_finish(proxy,res,&error))
	{

		error_message = g_strdup_printf ("%s %s",base_error,error->message);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
	}
}
//Backup partition to image file 
gboolean sysbak_admin_extfs_ptf_async (SysbakAdmin *sysbak)
{
	const char  *source,*target;
	gboolean     overwrite;
	SysbakGdbus *proxy;
	g_autofree gchar *error_message = NULL;
	const char  *base_error = "Backup partition to file failed";
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
	
	source = sysbak_admin_get_source (sysbak);
	target = sysbak_admin_get_target (sysbak);
	overwrite = sysbak_admin_get_option (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
    
    if (!check_file_device (source))
    {
		error_message = g_strdup_printf ("%s %s device does not exist",base_error,source);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }  
    if(check_file_device (target) && !overwrite)
    {
		error_message = g_strdup_printf ("%s Overwrite is not set, but the %s file already exists",base_error,target);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }       
    if (check_device_mount (source))
    {
		error_message = g_strdup_printf ("%s Please umount the %s to be backed up",base_error,source);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }   
	sysbak_gdbus_call_sysbak_extfs_ptf (proxy,
									    source,
									    target,
                                        overwrite,
										NULL,
								        (GAsyncReadyCallback) call_sysbak_extfs_ptf,
										sysbak);

    return TRUE;      /// finish
}
static void call_sysbak_extfs_ptp (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      data)
{
	SysbakAdmin *sysbak = SYSBAK_ADMIN (data);
	SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
	g_autofree gchar *error_message = NULL;
	const char  *base_error = "Backup partition to partition failed";
	
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
	if (! sysbak_gdbus_call_sysbak_extfs_ptp_finish(proxy,res,&error))
	{

		error_message = g_strdup_printf ("%s %s",base_error,error->message);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
	}
}

gboolean sysbak_admin_extfs_ptp_async (SysbakAdmin *sysbak)
{
	const char  *source,*target;
	gboolean     overwrite;
	SysbakGdbus *proxy;
	g_autofree gchar *error_message = NULL;
	const char  *base_error = "Backup partition to partition failed";
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
	
	source = sysbak_admin_get_source (sysbak);
	target = sysbak_admin_get_target (sysbak);
	overwrite = sysbak_admin_get_option (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (source))
    {
		error_message = g_strdup_printf ("%s %s device does not exist",base_error,source);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }   
    if (!check_file_device (target))
    {
		error_message = g_strdup_printf ("%s %s device does not exist",base_error,target);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }    
    if (check_device_mount (source))
    {
		error_message = g_strdup_printf ("%s Please umount the %s to be backed up",base_error,source);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }    
    if (check_device_mount (target))
    {
		error_message = g_strdup_printf ("%s Please umount the %s to be backed up",base_error,target);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }    

	sysbak_gdbus_call_sysbak_extfs_ptp (proxy,
									    source,
									    target,
                                        overwrite,
										NULL,
								        (GAsyncReadyCallback) call_sysbak_extfs_ptp,
										sysbak);

    return TRUE;      /// finish
}
static void call_sysbak_restore  (GObject      *source_object,
                                  GAsyncResult *res,
                                  gpointer      data)
{
	SysbakAdmin *sysbak = SYSBAK_ADMIN (data);
	SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
	g_autofree gchar *error_message = NULL;
	const char  *base_error = "Restore image to partition failed";
	
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
	if (! sysbak_gdbus_call_sysbak_restore_finish(proxy,res,&error))
	{

		error_message = g_strdup_printf ("%s %s",base_error,error->message);
//		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
	}
}
static gboolean is_lvm (const char *dev_name)
{
    int i = 0;
    int mode = 0;

    while (dev_name[i] != '\0')
    {
        if (dev_name[i] == '/')
            mode++;
        i++;
    }    
    
    if (mode == 1)
        return FALSE;

    return TRUE;
}    
gboolean sysbak_admin_restore_async (SysbakAdmin *sysbak)
{
	const char  *source,*target;
	gboolean     overwrite;
	SysbakGdbus *proxy;
	g_autofree gchar *error_message = NULL;
	const char  *base_error = "Restore image to partition failed";
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
	
	source = sysbak_admin_get_source (sysbak);
	target = sysbak_admin_get_target (sysbak);
	overwrite = sysbak_admin_get_option (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
    
    if (!check_file_device (source))
    {
		error_message = g_strdup_printf ("%s %s device does not exist",base_error,source);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }   
    if (!is_lvm(target))
    {    
        if (!check_file_device (target))
        {
		    error_message = g_strdup_printf ("%s %s device does not exist",base_error,target);
		    sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
            return FALSE;
        } 
    }    
    if (check_device_mount (target))
    {
		error_message = g_strdup_printf ("%s Please umount the %s to be backed up",base_error,target);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }    
	sysbak_gdbus_call_sysbak_restore (proxy,
								      source,
									  target,
                                      overwrite,
									  NULL,
								     (GAsyncReadyCallback) call_sysbak_restore,
									  sysbak);

    return TRUE;      /// finish
}
