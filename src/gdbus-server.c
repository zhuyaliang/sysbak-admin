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
#include <gio/gio.h>
#include <glib.h>
#include <fcntl.h>
#include <assert.h>
#include "gdbus-extfs.h"
#include "gdbus-disk.h"
#include "gdbus-fatfs.h"
#include "gdbus-btrfs.h"
#include "gdbus-xfsfs.h"

#define ORG_NAME  "org.sysbak.admin.gdbus"
#define DBS_NAME  "/org/sysbak/admin/gdbus"

static GMainLoop* loop = NULL;
 
static void AcquiredCallback (GDBusConnection *Connection,
                              const gchar     *name,
                              gpointer         data)
{
    GError           *error = NULL;
    SysbakGdbus      *sysbak_gdbus;
    SysbakGdbusIface *iface;

    sysbak_gdbus = sysbak_gdbus_skeleton_new ();
    iface = SYSBAK_GDBUS_GET_IFACE (sysbak_gdbus);

    iface->handle_sysbak_extfs_ptf  = gdbus_sysbak_extfs_ptf;
	iface->handle_sysbak_extfs_ptp  = gdbus_sysbak_extfs_ptp;
    iface->handle_sysbak_fatfs_ptf  = gdbus_sysbak_fatfs_ptf;
	iface->handle_sysbak_fatfs_ptp  = gdbus_sysbak_fatfs_ptp;
    iface->handle_sysbak_btrfs_ptf  = gdbus_sysbak_btrfs_ptf;
	iface->handle_sysbak_btrfs_ptp  = gdbus_sysbak_btrfs_ptp;
    iface->handle_sysbak_xfsfs_ptf  = gdbus_sysbak_xfsfs_ptf;
	iface->handle_sysbak_xfsfs_ptp  = gdbus_sysbak_xfsfs_ptp;
	iface->handle_sysbak_restore    = gdbus_sysbak_restore;
    iface->handle_get_disk_size     = gdbus_get_disk_size;
    iface->handle_get_source_use_size     = gdbus_get_source_use_size;
    iface->handle_create_pv         = gdbus_create_pv;
    iface->handle_restart_vg        = gdbus_restart_vg;
    iface->handle_backup_partition_table  = gdbus_backup_partition_table;
    iface->handle_restore_partition_table = gdbus_restore_partition_table;
    iface->handle_backup_disk_mbr   = gdbus_backup_disk_mbr;
	iface->handle_backup_lvm_meta   = gdbus_backup_lvm_meta;
	iface->handle_restore_lvm_meta  = gdbus_restore_lvm_meta;
    iface->handle_remove_all_vg     = gdbus_remove_all_vg;
    iface->handle_search_file_data  = gdbus_search_file_data;

    iface->handle_get_extfs_device_info   = gdbus_get_extfs_device_info;
    iface->handle_get_fs_image_info = gdbus_get_fs_image_info; 
    if(!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(sysbak_gdbus), 
                                         Connection, 
                                         DBS_NAME, 
                                         &error))
    {
        if(error != NULL)
        {
            g_warning("Error: Failed to export object. Reason: %s.\n", error->message);
            g_error_free(error);
        }    
        g_error("qiut g_dbus_interface_skeleton_export!!!\r\n");
    }    
    sysbak_gdbus_set_version (sysbak_gdbus,"v1.0.0");
}
static void NameLostCallback (GDBusConnection *connection,
                              const gchar     *name,
                              gpointer         data)
{
    g_warning("Lost Lost !!!!\r\n");
    g_main_loop_quit (loop);
}

int main(int argc,char* argv[])
{
    guint  dbus_id;

    dbus_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                              ORG_NAME,
                              G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
                              AcquiredCallback,
                              NULL,
                              NameLostCallback,
                              NULL,
                              NULL);
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_bus_unown_name(dbus_id);
    return 0;
}
