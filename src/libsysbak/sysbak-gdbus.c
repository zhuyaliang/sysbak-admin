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

#include "sysbak-gdbus.h"
#include "io-generated.h"
#define ORG_NAME  "org.io.operation.gdbus"
#define DBS_NAME  "/org/io/operation/gdbus"

IoGdbus   *proxy;
gboolean init_sysbak_gdbus (GError **error)
{
	GDBusConnection *connection;

    connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
    if (connection == NULL)
    {
        return FALSE;
    }
	proxy = io_gdbus_proxy_new_sync (connection,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     ORG_NAME,
                                     DBS_NAME,
                                     NULL,
                                     error);

	if (!proxy)
	{
		return FALSE;
	}
    g_object_set (proxy,"g-default-timeout", G_MAXINT,NULL);
	return TRUE;
}
device_info *get_extfs_device_info (const char *dev_name)
{
    device_info *dev_info;
	g_autoptr(GError) error = NULL;
	ull  totalblock;
	ull  usedblocks;
	uint block_size;
    g_return_val_if_fail (dev_name != NULL,FALSE);


	if (!io_gdbus_call_get_extfs_device_info_sync (proxy,
			                                       dev_name,
											      &totalblock,
											      &usedblocks,
											      &block_size,
											       NULL,
											      &error))
	{
		g_warning ("get_extfs_device_info faild %s\r\n",error->message);
		return NULL;
	}
    dev_info = g_slice_new (device_info);
    dev_info->totalblock = totalblock;
    dev_info->usedblocks = usedblocks;
    dev_info->block_size = block_size;
    memcpy (dev_info->fs,extfs_MAGIC,strlen (extfs_MAGIC) +1);
    g_print ("totalblock = %llu,usedblocks = %llu,block_size = %u\r\n",totalblock,usedblocks,block_size);
    return dev_info;
}    

device_info *get_extfs_image_info (const char *image_name)
{

    device_info *dev_info;
	g_autoptr(GError) error = NULL;
	ull  totalblock;
	ull  usedblocks;
	uint block_size;

	if (!io_gdbus_call_get_extfs_image_info_sync (proxy,
			                                      image_name,
											      &totalblock,
											      &usedblocks,
											      &block_size,
											      NULL,
											      &error))
	{
		g_warning ("get_extfs_image_info faild %s\r\n",error->message);
		return NULL;
	}
    dev_info = g_slice_new (device_info);
    dev_info->totalblock = totalblock;
    dev_info->usedblocks = usedblocks;
    dev_info->block_size = block_size;
    memcpy (dev_info->fs,extfs_MAGIC,strlen (extfs_MAGIC) +1);

    return dev_info;
}    

gboolean libgdbus_sysbak_extfs_ptf (const char *source,
		                            const char *target,
								    gboolean    overwrite,
                                    GError    **error)
{
	int state;
    
	if (!io_gdbus_call_sysbak_extfs_ptf_sync (proxy,
                                              source,
                                              target,
                                              overwrite,
											 &state,
											  NULL,
											  error))
	{
		return FALSE;
	}
    return TRUE;
}
gboolean libgdbus_sysbak_extfs_restore (const char *source,
                                        const char *target,
                                        gboolean    overwrite,
                                        GError    **error)
{
    int state;
	if (!io_gdbus_call_sysbak_restore_sync (proxy,
				                            source,
										    target,
                                            overwrite,
											&state,
											NULL,
											error))
	{
		return FALSE;
	}
    g_print ("state ==================%d\r\n",state);
	return TRUE;
}    
gboolean gdbus_sysbak_extfs_ptp (const char *source,
		                         const char *target,
								 gboolean    overwrite,
                                 GError    **error)
{
	int state;

	if (!io_gdbus_call_sysbak_extfs_ptp_sync (proxy,
											  source,
											  target,
                                              overwrite,
											 &state,
											  NULL,
											  error))
	{
		return FALSE;
	}
	return TRUE;
}

ull libgdbus_get_extfs_read_size (void)
{
	ull len;

	if (!io_gdbus_call_get_extfs_read_szie_sync (proxy,
									            &len,
									             NULL,
									             NULL))
	{
		return 0;
	}
	return len;
}
