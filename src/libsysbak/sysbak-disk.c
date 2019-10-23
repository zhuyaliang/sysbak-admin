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
#include <json-c/json_tokener.h>
#include <json-c/json.h>
#include <json-c/bits.h>

#include "sysbak-disk.h"
#include "sysbak-xfsfs.h"
#include "sysbak-extfs.h"
#include "sysbak-btrfs.h"
#include "sysbak-fatfs.h"
#include "sysbak-admin-generated.h"

#define   CMDOPTION   "NAME,FSTYPE,UUID";

typedef void (*sysbak_func) (SysbakAdmin *);
typedef struct
{
    char        *fs_type;
    sysbak_func  sysbak_admin_type;
}sys_admin;

static const char *execute_command_line (const char *disk_name)
{
    const gchar *argv[7];
    gint         status;
    GError      *error = NULL;
    gchar       *standard_output;

    argv[0] = "/usr/bin/lsblk";
    argv[1] = "-o";
    argv[2] = CMDOPTION;
    argv[3] = disk_name;
    argv[4] = "-Jl";
    argv[5] = NULL;

    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, &standard_output,NULL,&status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;
    
    return standard_output;
ERROR:
    g_error_free (error);
    return NULL;
} 
static const char *group_name;
static void record_standard_disk_info (json_object * jvalue,GKeyFile *Kconfig)
{
    const char *value;

    json_object_object_foreach(jvalue, key, vale)
    {
        if (g_strcmp0 (key,"name") == 0)
        {
            value = json_object_get_string(vale);
            if (g_strrstr (value,"-") != NULL || strlen (value) == 3)
            {
                return;
            }
            group_name = &value[3];
        }

        g_key_file_set_string(Kconfig,
                              group_name,
                              key,
                              json_object_get_string(vale));
    }
}
static void record_lvm_disk_info (json_object * jvalue,GKeyFile *Kconfig)
{
    const char *value;
    json_object_object_foreach(jvalue, key, vale)
    {
        if (g_strcmp0 (key,"name") == 0)
        {
            value = json_object_get_string(vale);
            if (g_strrstr (value,"-") == NULL)
            {
                return;
            }
            group_name = value;
        }
        g_key_file_set_string(Kconfig,
                              group_name,
                              key,
                              json_object_get_string(vale));
    }
}

static gboolean json_parse_array(json_object *jobj, char *key,const char *config_name)
{
    int i;
    json_object *jvalue;
    json_object *jarray = jobj;
    int          arraylen;
    gboolean     ret;
    GKeyFile    *Kconfig = NULL;
    Kconfig = g_key_file_new();
    
    if(key)
    {
        jarray = json_object_object_get(jobj, key); /*Getting the array if it is a key value pair*/
    }
    arraylen = json_object_array_length(jarray); /*Getting the length of the array*/
    for (i = 0; i< arraylen; i++)
    {
        jvalue = json_object_array_get_idx(jarray, i); /*Getting the array element at position i*/
        record_standard_disk_info (jvalue,Kconfig);
    }
    g_key_file_set_string(Kconfig,
                          "end",
                          "name",
                          "lvm");

    g_key_file_set_string(Kconfig,
                          "end",
                          "fstype",
                          "lvm");
    for (i=0; i< arraylen; i++)
    {
        jvalue = json_object_array_get_idx(jarray, i); /*Getting the array element at position i*/
        record_lvm_disk_info (jvalue,Kconfig);
    }
    ret = g_key_file_save_to_file(Kconfig,config_name,NULL);
    g_key_file_free(Kconfig);

    return ret;
}

gboolean get_disk_info_config (const char *disk_name,const char *config_name)
{
    const char    *cmd_data = NULL;
    json_object   *jobj;
    
    g_return_val_if_fail (config_name  != NULL,FALSE);
    g_return_val_if_fail (disk_name != NULL,FALSE);
    
    if (!check_file_device (disk_name))
    {
        return FALSE;
    }    
    cmd_data =  execute_command_line (disk_name);
    if (cmd_data == NULL)
    {
        return FALSE;
    }   
    
    jobj = json_tokener_parse(cmd_data);
    if (is_error (jobj))
    {
        return FALSE;
    }
    json_object_object_foreach(jobj, key, vale)
    {
        if (!json_parse_array(jobj, key,config_name))
        {
            return FALSE;
        }    
    }
    
    return TRUE;
}

gboolean get_disk_mbr (SysbakAdmin *sysbak)
{
    const char  *source,*target;
    SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
    gboolean     ret;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    source = sysbak_admin_get_source (sysbak);
    target = sysbak_admin_get_target (sysbak);
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (source))
    {
        return FALSE;
    }
	if (!sysbak_gdbus_call_backup_disk_mbr_sync (proxy,
                                                 source,
                                                 target,
											     &ret,
                                                 NULL,
											    &error))
	{
		return FALSE;
	}
    
    return ret;      /// finish
}

gboolean get_disk_partition_table (SysbakAdmin *sysbak)
{
    const char  *source,*target;
    SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
    gboolean     ret;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    source = sysbak_admin_get_source (sysbak);
    target = sysbak_admin_get_target (sysbak);
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (source))
    {
        return FALSE;
    }
	if (!sysbak_gdbus_call_backup_partition_table_sync (proxy,
                                                        source,
                                                        target,
											            &ret,
                                                        NULL,
											            &error))
	{
		return FALSE;
	}
    
    return ret;      /// finish
}    

gboolean get_disk_lvm_metadata (SysbakAdmin *sysbak)
{
    const char  *target;
    SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
    gboolean     ret;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    target = sysbak_admin_get_target (sysbak);
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
    
	if (!sysbak_gdbus_call_backup_lvm_meta_sync (proxy,
                                                 target,
											    &ret,
                                                 NULL,
											    &error))
	{
		return FALSE;
	}
    
    return ret;      /// finish
}  

static void sysbak_admin_extfs (SysbakAdmin *sysbak)
{
    sysbak_admin_extfs_ptf_async (sysbak);
}    
static void sysbak_admin_fatfs (SysbakAdmin *sysbak) 
{
    sysbak_admin_fatfs_ptf_async (sysbak);
}
static void sysbak_admin_btrfs (SysbakAdmin *sysbak) 
{
    sysbak_admin_btrfs_ptf_async (sysbak);
}
static void sysbak_admin_xfsfs (SysbakAdmin *sysbak) 
{
    sysbak_admin_xfsfs_ptf_async (sysbak);
}
static void character_replace (char *str)
{
    int i = 0;

    while (str[i] != '\0')
    {
        if (str[i] == '-')
        {    
            str[i] = '/';
            break;
        }
        i++;
    }    
}   

static void sysbak_admin_disk_data (SysbakAdmin  *sysbak,char *dir_name)
{
    char     *disk_info;
    GKeyFile *keyfile;
    char    **groups = NULL;
    GError   *error = NULL;
    gsize     length = 0;
    int       i,j;
    char     *partition;
    char     *fs_type;
    char     *source,*target;	
    
    static sys_admin array_data [6] =
    {
        {"ext2", sysbak_admin_extfs},
        {"ext3", sysbak_admin_extfs},
        {"ext4", sysbak_admin_extfs},
        {"vfat", sysbak_admin_fatfs},
        {"btrfs",sysbak_admin_btrfs},
        {"xfs",  sysbak_admin_xfsfs}
    };
    
    disk_info = g_strdup_printf ("%s/disk-info.ini",dir_name);
    keyfile = g_key_file_new();
    g_key_file_load_from_file(keyfile,disk_info, G_KEY_FILE_NONE, &error);
    groups = g_key_file_get_groups(keyfile, &length);
    for(i = 0; i < (int)length; i++)
    {
        partition = g_key_file_get_string (keyfile,
                                           groups[i],
                                          "name",
                                           NULL);
        
        fs_type = g_key_file_get_string (keyfile,
                                         groups[i],
                                        "fstype",
                                         &error);
        
        for (j = 0; j < 6; j++)
        {
            if (g_strcmp0 (fs_type,array_data[j].fs_type) == 0)
            {
                target = g_strdup_printf ("%s/%s.img",dir_name,partition);
                character_replace (partition);
                source = g_strdup_printf ("/dev/%s",partition);

	            sysbak_admin_set_source (sysbak,source);
	            sysbak_admin_set_target (sysbak,target);
                sysbak_admin_set_option (sysbak,1);
                array_data[j].sysbak_admin_type (sysbak);
                g_free (source);
                g_free (target);
            }    
        }    
    }
    g_free (disk_info);
}    
gboolean sysbak_admin_disk_to_file (SysbakAdmin  *sysbak)
{
    gboolean  ret;
    char     *disk_info,*disk_mbr,*disk_table,*lvm_meta;
    char     *source,*target;	
    
    source = sysbak_admin_get_source (sysbak);
    target = sysbak_admin_get_target (sysbak);

    if (!check_file_device (source))
    {
        return FALSE;
    }
    // stp 1 Check if the file exists;
    // stp 2 Check overwrite;
    // stp 3 Check disk space;

    lvm_meta = g_strdup_printf ("%s/lvm",target);
	sysbak_admin_set_target (sysbak,lvm_meta);
    ret = get_disk_lvm_metadata (sysbak);
    g_free (lvm_meta);
    if (!ret)
    {
        return FALSE;
    }    
    
    disk_info = g_strdup_printf ("%s/disk-info.ini",target);
    ret = get_disk_info_config (source,disk_info);
    g_free (disk_info);
    if (!ret)
    {
        return FALSE;
    }    
    
    
    disk_table = g_strdup_printf ("%s/disk-table",target);
    sysbak_admin_set_target (sysbak,disk_table);
    ret = get_disk_partition_table (sysbak);
    g_free (disk_table);
    if (!ret)
    {
        return FALSE;
    }    
    
    disk_mbr = g_strdup_printf ("%s/disk-mbr",target);
	sysbak_admin_set_target (sysbak,disk_mbr);
    ret = get_disk_mbr (sysbak);
    g_free (disk_mbr);
    if (!ret)
    {
        return FALSE;
    }    
    sysbak_admin_disk_data (sysbak,target);

    return TRUE;
}
/*
 *  sfdisk source < /target
 *
 */
gboolean set_disk_partition_table (SysbakAdmin *sysbak)
{
    const char  *source,*target;
    SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
    gboolean     ret;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    source = sysbak_admin_get_source (sysbak);
    target = sysbak_admin_get_target (sysbak);
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (source))
    {
        return FALSE;
    }
    if (!check_file_device (target))
    {
        return FALSE;
    }
	if (!sysbak_gdbus_call_restore_partition_table_sync (proxy,
                                                         source,
                                                         target,
											             &ret,
                                                         NULL,
											             &error))
	{
		return FALSE;
	}
    
    return ret;      /// finish
}    
/*
 *  pvcreate --restorefile source --uuid uuid target
 *
 */
gboolean create_lvm_pv (SysbakAdmin *sysbak,const char *uuid)
{
    const char  *source,*target;
    SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
    gboolean     ret;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);
    g_return_val_if_fail (uuid != NULL,FALSE);

    source = sysbak_admin_get_source (sysbak);
    target = sysbak_admin_get_target (sysbak);
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (target))
    {
        return FALSE;
    }
    if (!check_file_device (source))
    {
        return FALSE;
    }
	if (!sysbak_gdbus_call_create_pv_sync (proxy,
                                           source,
                                           uuid,
                                           target,
										   &ret,
                                           NULL,
										   &error))
	{
		return FALSE;
	}
    
    return ret;      /// finish
}

/*
 *  vgcfgrestore -f source target
 *
 */
gboolean set_disk_lvm_metadata (SysbakAdmin *sysbak)
{
    const char  *source,*target;
    SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
    gboolean     ret;
    char        *dev_path;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    source = sysbak_admin_get_source (sysbak);
    target = sysbak_admin_get_target (sysbak);
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    dev_path = g_strdup_printf ("/dev/%s",target);
    if (!check_file_device (dev_path))
    {
        g_free (dev_path);
        return FALSE;
    }
    if (!check_file_device (source))
    {
        g_free (dev_path);
        return FALSE;
    }
	if (!sysbak_gdbus_call_restore_lvm_meta_sync (proxy,
                                                  source,
                                                  target,
										          &ret,
                                                  NULL,
										          &error))
	{
        g_free (dev_path);
		return FALSE;
	}
    
    g_free (dev_path);
    return ret;      /// finish
}

/*
 *  vgchange -ay source 
 *  lvchange -ay source 
 */
gboolean restart_lvm_vg (SysbakAdmin *sysbak)
{
    const char  *source;
    SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
    gboolean     ret;
    char        *dev_path;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    source = sysbak_admin_get_source (sysbak);
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    dev_path = g_strdup_printf ("/dev/%s",source);
    if (!check_file_device (dev_path))
    {
        g_free (dev_path);
        return FALSE;
    }
	if (!sysbak_gdbus_call_restart_vg_sync (proxy,
                                            source,
										    &ret,
                                            NULL,
										    &error))
    {
        g_free (dev_path);
        return FALSE;
    }       

    g_free (dev_path);

    return TRUE;

}    
