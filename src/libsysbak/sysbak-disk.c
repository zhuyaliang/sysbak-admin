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
// dd if=source of=target bs=2048 count=1
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
static char *get_dev_path (char *str)
{
    int i = 0;
    const char *normal_path = "/dev/";
    const char *lvm_path = "/dev/mapper/";

    char *dev_name;
    
    dev_name = g_strconcat(normal_path,str,NULL);
    while (str[i] != '\0')
    {
        if (str[i] == '-')
        { 
            g_free (dev_name);
            dev_name = g_strconcat(lvm_path,str,NULL);
            break;
        }
        i++;
    }

    return dev_name;
/*
    while (str[i] != '\0')
    {
        if (str[i] == '-')
        {    
            str[i] = '/';
            break;
        }
        i++;
    } 
  */  
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
                source = get_dev_path (partition);
                //source = g_strdup_printf ("/dev/%s",partition);

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
static gboolean create_target_dir (const char *target)
{
    if (access (target,F_OK) == -1)
    {
        return mkdir (target,S_IRWXU);
    }
    return -1;
    
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
    if (create_target_dir (target) < 0 )
    {
        return FALSE;
    }    
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

static void call_restore_lvm_meta (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      data)
{
	SysbakAdmin *sysbak = SYSBAK_ADMIN (data);
	SysbakGdbus *proxy;
    gboolean     ret;
    g_autoptr(GError) error = NULL;
	g_autofree gchar *error_message = NULL;
	const char  *base_error = "restore lvm meta failed";
	
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
	if (! sysbak_gdbus_call_restore_lvm_meta_finish(proxy,&ret,NULL,&error))
	{

		error_message = g_strdup_printf ("%s %s",base_error,error->message);
		sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
	}
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
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    source = sysbak_admin_get_source (sysbak);
    target = sysbak_admin_get_target (sysbak);
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (source))
    {
        return FALSE;
    }
	sysbak_gdbus_call_restore_lvm_meta (proxy,
                                        source,
                                        target,
								        NULL,
                                        NULL,NULL);
                                       // (GAsyncReadyCallback) call_restore_lvm_meta,
								       // sysbak);
    
    return TRUE;      /// finish
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
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    source = sysbak_admin_get_source (sysbak);
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

	sysbak_gdbus_call_restart_vg (proxy,
                                  source,
							      NULL,
                                  NULL,
								  NULL);
    return TRUE;
}

guint64 get_source_space_size (SysbakAdmin *sysbak,const char *config_path)
{
    SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
    guint64      size;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (config_path))
    {
        return 0;
    }
	if (!sysbak_gdbus_call_get_source_use_size_sync(proxy,
                                                    config_path,
										            &size,
                                                    NULL,
										            &error))
    {
        return 0;
    }       

    return size;

}

guint64 get_disk_space_size (SysbakAdmin *sysbak,const char *dev_name)
{
    SysbakGdbus *proxy;
    g_autoptr(GError) error = NULL;
    guint64      size;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (dev_name))
    {
        return 0;
    }
	if (!sysbak_gdbus_call_get_disk_size_sync(proxy,
                                              dev_name,
										      &size,
                                               NULL,
										      &error))
    {
        return 0;
    }       

    return size;

}
static gboolean restore_partition_table (const char *source,const char *target)
{
    SysbakAdmin *sysbak;

    sysbak = sysbak_admin_new ();
    sysbak_admin_set_source (sysbak,target);//dev
    sysbak_admin_set_target (sysbak,source);//pt

    return set_disk_partition_table (sysbak);
    
}   
static gboolean restore_disk_mbr (const char *source,const char *target)
{
    SysbakAdmin *sysbak;
    char        *mbr;

    mbr = g_strdup_printf ("%s/disk-mbr",source);
    sysbak = sysbak_admin_new ();
    sysbak_admin_set_source (sysbak,mbr);//mbr
    sysbak_admin_set_target (sysbak,target);//dev
    g_free (mbr);

    return get_disk_mbr (sysbak); 

}  
static gboolean activation_lvm_pv (SysbakAdmin *sysbak,const char *dir_path)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GDir)   dir;
    const gchar      *fn;
    dir = g_dir_open (dir_path, 0, &error);
    if (dir == NULL)
    {
        return FALSE;
    }
    while ((fn = g_dir_read_name (dir)) != NULL)
    {
        if (g_str_has_prefix (fn, "lvm-"))
        {
            g_autofree gchar *filename = g_build_filename (dir_path, fn, NULL);
            sysbak_admin_set_source (sysbak,filename);//vgcfg file
            sysbak_admin_set_target (sysbak,&fn[4]);//vg name
            set_disk_lvm_metadata (sysbak);
            sysbak_admin_set_source (sysbak,&fn[4]);//vg name
            restart_lvm_vg (sysbak);
        }
    }

    return TRUE;
}   
static gboolean is_vg (SysbakGdbus *proxy,const char *file_name,const char *uuid)
{
    gboolean ret;

	if (!sysbak_gdbus_call_search_file_data_sync (proxy,
                                                  file_name,
                                                  uuid,
                                                  &ret,
                                                  NULL,
                                                  NULL))
    {
        return FALSE;
    }
    return ret;
}    
static gboolean restore_lvm_pv (const char *dir_path,const char *dev_name,const char *uuid)
{
    SysbakAdmin      *sysbak;
    
    g_autoptr(GError) error = NULL;
    g_autoptr(GDir)   dir;
    const gchar      *fn;
    SysbakGdbus      *proxy;
    
    dir = g_dir_open (dir_path, 0, &error);
    if (dir == NULL)
    {
        return FALSE;
    }
    sysbak = sysbak_admin_new ();
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
    while ((fn = g_dir_read_name (dir)) != NULL)
    {
        if (g_str_has_prefix (fn, "lvm-"))
        {
            g_autofree gchar *filename = g_build_filename (dir_path, fn, NULL);
            if (is_vg (proxy,filename,uuid))
            {    
                sysbak_admin_set_source (sysbak,filename);//vgcfg file name
                sysbak_admin_set_target (sysbak,dev_name);//vg name
                if ( !create_lvm_pv (sysbak,uuid))
                {
                    return FALSE;
                }   
                return TRUE;
            }    
        }
    }

    return FALSE;

}   

static gboolean restore_disk_partition_data (SysbakAdmin *sysbak,
                                             const char  *file_name,
                                             const char  *dev_name,
                                             const char  *fs_type)
{
    sysbak_admin_set_source (sysbak,file_name);
	sysbak_admin_set_target (sysbak,dev_name);
	sysbak_admin_set_option (sysbak,1);
    return sysbak_admin_restore_async (sysbak);
}    
static gboolean is_lvm (const char *group)
{
    if (g_strrstr (group,"-") != NULL)
    {
        return TRUE;
    }    
    return FALSE;
}    
static gboolean read_cfg_file_restore (SysbakAdmin *sysbak,const char *source,const char *target)  
{
    GKeyFile     *kconfig = NULL;
    char         *disk_info = NULL;
    GError       *error = NULL;
    g_auto(GStrv) groups = NULL;
    gsize         length = 0;
    char         *fs_type;
    char         *uuid;
    char         *dev_name;
    char         *name;
    char         *file_name;

    kconfig = g_key_file_new();
    if(kconfig == NULL)
    {
        goto EXIT;
    }
    disk_info = g_strdup_printf ("%s/disk-info.ini",source);
    if (!check_file_device (disk_info))
    {
        goto EXIT;
    }
    if(!g_key_file_load_from_file(kconfig, disk_info, G_KEY_FILE_NONE, &error))
    {
        goto EXIT;
    }
    groups = g_key_file_get_groups(kconfig, &length);
    for (int i = 0; i < (int)length; i++)
    {
        if (g_strcmp0 (groups[i],"end") == 0)
        {
            if (!activation_lvm_pv (sysbak,source))
            {
                return FALSE;
            }

            continue;
        }    
        fs_type = g_key_file_get_string(kconfig,
                                        groups[i],
                                       "fstype",
                                       &error);
        if (g_strcmp0 (fs_type,"LVM2_member") == 0)
        {
            uuid = g_key_file_get_string(kconfig,
                                         groups[i],
                                         "uuid",
                                         &error);
            dev_name = g_strconcat (target,groups[i],NULL);
            restore_lvm_pv (source,dev_name,uuid);
            g_free (dev_name);
            continue;
        }  
        name = g_key_file_get_string(kconfig,
                                     groups[i],
                                     "name",
                                     &error);
        if (is_lvm (groups[i]))
        {
            dev_name = g_strconcat ("/dev/mapper/",groups[i],NULL); 
        }
        else
        {    
            dev_name = g_strconcat (target,groups[i],NULL);
        }
        file_name = g_strconcat (source,"/",name,".img",NULL);
        restore_disk_partition_data (sysbak,file_name,dev_name,fs_type);
        g_free (dev_name);
        g_free (file_name);
    }
EXIT:    
    if (kconfig != NULL)
    {
        g_key_file_free (kconfig);
    }
    if (disk_info != NULL)
    {
        g_free (disk_info);
    }    
    
    return FALSE;
}   
static gboolean check_dev_is_mount (const char *source,char **error_message)
{
    GKeyFile     *kconfig = NULL;
    char         *disk_info = NULL;
    GError       *error = NULL;
    g_auto(GStrv) groups = NULL;
    gsize         length = 0;
    char         *name;
    char         *dev_name = NULL;
    gboolean      ret = TRUE;

    kconfig = g_key_file_new();
    if(kconfig == NULL)
    {
        goto EXIT;
    }
    disk_info = g_strdup_printf ("%s/disk-info.ini",source);
    if (!check_file_permission (disk_info))
    {
        *error_message = "source dir Permission denied";
        goto EXIT;
    }    
    if (!check_file_device (disk_info))
    {
        *error_message = "source dir Non-existent";
        goto EXIT;
    }
    if(!g_key_file_load_from_file(kconfig, disk_info, G_KEY_FILE_NONE, &error))
    {
        *error_message = error->message; 
        goto EXIT;
    }
    groups = g_key_file_get_groups(kconfig, &length);
    for (int i = 0; i < (int)length; i++)
    {
        name = g_key_file_get_string(kconfig,
                                     groups[i],
                                     "name",
                                     &error);
        if (g_strcmp0 (name,"lvm") == 0)
        {
            break;
        }   
        if (is_lvm (name))
        {
            dev_name = g_strconcat ("/dev/mapper/",name,NULL); 
        }
        else
        {    
            dev_name = g_strconcat ("/dev/",name,NULL);
        }
        if (check_device_mount (dev_name))
        {
            *error_message = "Device in use";
            g_free (dev_name);
            goto EXIT; 
        }    
        g_free (dev_name);
        dev_name = NULL;
    }
    ret = FALSE;
EXIT:    
    if (kconfig != NULL)
    {
        g_key_file_free (kconfig);
    }
    if (disk_info != NULL)
    {
        g_free (disk_info);
    }    
    return ret;
    
}    
static void remove_disk_old_lvm (SysbakAdmin *sysbak,const char *disk_name)
{
    SysbakGdbus *proxy;
    
    proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);
    sysbak_gdbus_call_remove_all_vg (proxy,disk_name,NULL,NULL,NULL);
}
gboolean sysbak_admin_restore_disk (SysbakAdmin *sysbak)
{
    const char  *source,*target;
	SysbakGdbus *proxy;
    guint64      needed_space,disk_space;
    g_autoptr(GError) error = NULL;
    char        *file_path;
    char        *error_message = NULL;
    
    g_return_val_if_fail (IS_SYSBAK_ADMIN (sysbak),FALSE);

    source = sysbak_admin_get_source (sysbak);
    target = sysbak_admin_get_target (sysbak);
	proxy  = (SysbakGdbus*)sysbak_admin_get_proxy (sysbak);

    if (!check_file_device (target))
    {
        sysbak_gdbus_emit_sysbak_error (proxy,"Device does not exist",-1);
        return FALSE;
    }
    if (!check_file_device (source))
    {
        sysbak_gdbus_emit_sysbak_error (proxy,"File does not exist",-1);
        return FALSE;
    }
    if (check_dev_is_mount (source,&error_message))
    {   
        sysbak_gdbus_emit_sysbak_error (proxy,error_message,-1);
        return FALSE;
    }    
    remove_disk_old_lvm (sysbak,target);
    file_path = g_strdup_printf ("%s/disk-table",source);
    needed_space = get_source_space_size (sysbak,file_path);
    disk_space = get_disk_space_size (sysbak,target);
    if (disk_space < needed_space)
    {
        g_free (file_path);
        sysbak_gdbus_emit_sysbak_error (proxy,"Insufficient storage space",-1);
        return FALSE;
    }   
    
    if (!restore_disk_mbr(source,target))
    {
        g_free (file_path);
        sysbak_gdbus_emit_sysbak_error (proxy,"Failed to recover the hard disk MBR",-1);
        return FALSE;
    }
    if (!restore_partition_table (file_path,target))
    {    
        g_free (file_path);
        return FALSE;
    }    
    g_free (file_path);
    read_cfg_file_restore (sysbak,source,target);
    return TRUE;
    //step 1 check source target it exist? 
    //setp 2 check source space size
    //step 3 dd if write mbr
    //step 4 sfdisk restaore pt
    //step 5 read disk-info.ini create pv restore image vgcgfrestore lvm restart lvm
} 

