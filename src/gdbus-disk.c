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

#include <fcntl.h>
#define _BUILDING_LVM
#include <lvm2app.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include "gdbus-share.h"
#include "gdbus-disk.h"

static const gchar *get_restore_cmd_option (const char *s,const char *t)
{
    return g_strdup_printf ("/usr/bin/sfdisk %s < %s",s,t);
}    
gboolean gdbus_restore_partition_table (SysbakGdbus           *object,
                                        GDBusMethodInvocation *invocation,
				 				        const gchar           *source,
								        const gchar           *target)
{
    const gchar *argv[4];
    gint         status;
    GError      *error = NULL;
    const gchar *cmd;
    gchar       *standard_error;

    cmd = get_restore_cmd_option (source,target);

    argv[0] = "/usr/bin/bash";
    argv[1] = "-c";
    argv[2] = cmd;
    argv[3] = NULL;
    
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL, &standard_error, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;

    sysbak_gdbus_complete_restore_partition_table (object,invocation,TRUE); 
    g_free (cmd);
    return TRUE;
ERROR:
    g_free (cmd);
    sysbak_gdbus_complete_restore_partition_table (object,invocation,FALSE);
    sysbak_gdbus_emit_sysbak_error (object,
                                    standard_error,
                                    1);
    g_error_free (error);
    return FALSE;
}  
static const gchar *get_backup_cmd_option (const char *s,const char *t)
{
    return g_strdup_printf ("/usr/bin/sfdisk -d %s > %s",s,t);
}    
gboolean gdbus_backup_partition_table (SysbakGdbus           *object,
                                       GDBusMethodInvocation *invocation,
								       const gchar           *source,
								       const gchar           *target)
{
    const gchar *argv[4];
    gint         status;
    GError      *error = NULL;
    const gchar *cmd;
    gchar       *standard_error;

    cmd = get_backup_cmd_option (source,target);

    argv[0] = "/usr/bin/bash";
    argv[1] = "-c";
    argv[2] = cmd;
    argv[3] = NULL;
    
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL, &standard_error, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;

    sysbak_gdbus_complete_backup_partition_table (object,invocation,TRUE); 
    g_free (cmd);
    return TRUE;
ERROR:
    g_free (cmd);
    sysbak_gdbus_complete_backup_partition_table (object,invocation,FALSE);
    sysbak_gdbus_emit_sysbak_error (object,
                                    standard_error,
                                    1);
    g_error_free (error);
    return FALSE;
}  
gboolean gdbus_backup_disk_mbr (SysbakGdbus           *object,
                                GDBusMethodInvocation *invocation,
							    const gchar           *source,
							    const gchar           *target)
{
    const gchar *argv[40];
    gint         status;
    GError      *error = NULL;
    gchar       *standard_error;
    const gchar *s,*t;
    
    s = g_strdup_printf ("if=%s",source);
    t = g_strdup_printf ("of=%s",target);
    argv[0] = "/usr/bin/dd";
    argv[1] = s;
    argv[2] = t;
    argv[3] = "bs=2048";
    argv[4] = "count=1";
    argv[5] = NULL;

    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL,&standard_error, &status, &error))
        goto ERROR;
    
    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;


    sysbak_gdbus_complete_backup_disk_mbr (object,invocation,TRUE); 
    g_free (s);
    g_free (t);
    return TRUE;
ERROR:
    g_free (s);
    g_free (t);
    sysbak_gdbus_complete_backup_disk_mbr (object,invocation,FALSE);
    sysbak_gdbus_emit_sysbak_error (object,
                                    standard_error,
                                    1);
    g_error_free (error);
    return FALSE;
}   
gboolean gdbus_restore_lvm_meta (SysbakGdbus           *object,
                                 GDBusMethodInvocation *invocation,
					 		     const gchar           *file,
                                 const gchar           *vgname)
{
    const gchar *argv[7];
    gint         status;
    GError      *error = NULL;

    argv[0] = "/sbin/vgcfgrestore";
    argv[1] = "-f";
    argv[2] = file;
	argv[3] = vgname;
    argv[4] = "-y";
    argv[5] = NULL;
    
    sysbak_gdbus_complete_restore_lvm_meta (object,invocation,TRUE); 
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL,NULL, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;
    return TRUE;
ERROR:
    sysbak_gdbus_complete_restore_lvm_meta (object,invocation,FALSE);
    sysbak_gdbus_emit_sysbak_error (object,
                                    "vgcfgrestore failed",
                                    1);
    g_error_free (error);
    return FALSE;
}  
gboolean gdbus_backup_lvm_meta (SysbakGdbus           *object,
                                GDBusMethodInvocation *invocation,
							    const gchar           *target)
{
    const gchar *argv[5];
    gint         status;
    GError      *error = NULL;
    gchar       *standard_error;
    const char  *s;
    const char  *base = "%s";

    s = g_strdup_printf ("%s-%s",target,base);
    argv[0] = "/sbin/vgcfgbackup";
    argv[1] = "-f";
    argv[2] = s;
	argv[3] = NULL;
    
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL, &standard_error, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;

    sysbak_gdbus_complete_backup_lvm_meta (object,invocation,TRUE); 
    g_free (s);
    return TRUE;
ERROR:
    g_free (s);
    sysbak_gdbus_complete_backup_lvm_meta (object,invocation,FALSE);
    sysbak_gdbus_emit_sysbak_error (object,
                                    standard_error,
                                    1);
    g_error_free (error);
    return FALSE;
}  
gboolean gdbus_create_pv (SysbakGdbus           *object,
                          GDBusMethodInvocation *invocation,
						  const gchar           *file,
                          const gchar           *uuid,
                          const gchar           *device)
{
    const gchar *argv[9];
    gint         status;
    GError      *error = NULL;

    argv[0] = "/sbin/pvcreate";
    argv[1] = "--restorefile";
    argv[2] = file;
	argv[3] = "--uuid";
    argv[4] = uuid;
    argv[5] = device;
    argv[6] = "-y";
    argv[7] = NULL;
    
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;

    sysbak_gdbus_complete_create_pv (object,invocation,TRUE); 
    return TRUE;
ERROR:
    sysbak_gdbus_complete_create_pv (object,invocation,FALSE);
    sysbak_gdbus_emit_sysbak_error (object,
                                    "create pv failed",
                                    1);
    g_error_free (error);
    return FALSE;

}    
gboolean gdbus_restart_vg (SysbakGdbus           *object,
                           GDBusMethodInvocation *invocation,
						   const gchar           *vgname)
{
    const gchar *argv[5];
    gint         status;
    GError      *error = NULL;

    argv[0] = "/sbin/vgchange";
    argv[1] = "-ay";
    argv[2] = vgname;
    argv[3] = NULL;
    
    sysbak_gdbus_complete_restart_vg (object,invocation,TRUE); 
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;

    argv[0] = "/sbin/lvchange";
    
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;
    return TRUE;
ERROR:
    sysbak_gdbus_complete_restart_vg (object,invocation,FALSE);
    sysbak_gdbus_emit_sysbak_error (object,
                                    "restart VG failed",
                                    1);
    g_error_free (error);
    return FALSE;

}
gboolean gdbus_get_disk_size (SysbakGdbus           *object,
                              GDBusMethodInvocation *invocation,
						      const gchar           *dev_name)
{
    int  fd;
    ull  size;

    fd = open(dev_name, O_RDONLY);
    if (fd < 0)
    {
        goto ERROR;
    }
    size = get_partition_free_space (&fd);
    if (size <= 0)
    {
        goto ERROR;
    }   
    sysbak_gdbus_complete_get_disk_size (object,invocation,size/1024);
    return TRUE;
ERROR:
    sysbak_gdbus_complete_get_disk_size (object,invocation,0);
    sysbak_gdbus_emit_sysbak_error (object,
                                    "get disk size failed",
                                    1);
    
    return FALSE;
}
static char *get_file_last_line (const char *file_path)
{
    FILE *fp;
    char *line;
    line = malloc (1024);
    if (line == NULL)
    {
        return NULL;
    }    
    fp = fopen(file_path, "r");
    if (fp == NULL)
    {
        return NULL;
    }    
    while(!feof(fp))
    {
        fgets(line,1024,fp);
        if(feof(fp))
            break;
   }
   fclose(fp);

   return line;

}   

static int get_spec_data (char *source,const char end)
{
    int   i = 0;
    int len = strlen (source);

    while (len --)
    {
        if (source[i] == end)
            break;
        i++;
    }
    if (len <= 0)
    {
        return 0;
    }    
    source[i] = '\0';
    return atoi (source);
}

gboolean gdbus_get_source_use_size (SysbakGdbus           *object,
                                    GDBusMethodInvocation *invocation,
						            const gchar           *file_path)
{
    ull    size;
    char  *data;
    char **str = NULL;
    int    start_size;
    int    sector_szie;

    data = get_file_last_line (file_path);
    if (data == NULL)
    {
        goto ERROR;
    }    
    str = g_strsplit(data,"=",-1);
    if (g_strv_length(str) < 2)
    {
        goto ERROR;
    }   
    start_size = get_spec_data (str[1],',');
    sector_szie = get_spec_data (str[2],',');
    
    size = (start_size + sector_szie) / 2;
    sysbak_gdbus_complete_get_source_use_size (object,invocation,size);
    g_strfreev (str);
    return TRUE;
ERROR:
    g_strfreev (str);
    sysbak_gdbus_complete_get_source_use_size (object,invocation,0);
    sysbak_gdbus_emit_sysbak_error (object,
                                    "get source use size failed",
                                    1);
    
    return FALSE;
}   
static gboolean shell_cmd_remove_vg (const char *vgname)
{
    const gchar *argv[4];
    gint         status;
    GError      *error = NULL;

    argv[0] = "/sbin/vgremove";
    argv[1] = vgname;
    argv[2] = "-y";
    argv[3] = NULL;
    
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;

    return TRUE;
ERROR:
    g_error_free (error);
    return FALSE;
}    
static gboolean shell_cmd_remove_pv (const char *pvname)
{
    const gchar *argv[4];
    gint         status;
    GError      *error = NULL;

    argv[0] = "/sbin/pvremove";
    argv[1] = pvname;
    argv[2] = "-y";
    argv[3] = NULL;
    
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;

    return TRUE;
ERROR:
    g_error_free (error);
    return FALSE;
}
gboolean gdbus_remove_all_vg (SysbakGdbus           *object,
                              GDBusMethodInvocation *invocation)
{
    lvm_t                libh;
    struct dm_list      *vgnames;
    struct lvm_str_list *vglist;
    struct dm_list      *pvs;
    struct lvm_pv_list  *pvlist;
    const char          *name;
    GPtrArray           *array;
    gboolean             ret = TRUE;
    uint i;
    libh = lvm_init(NULL);

    vgnames = lvm_list_vg_names(libh);
    if (vgnames == NULL)
    {
        ret = FALSE;
        goto EXITVG;
    }   
    pvs = lvm_list_pvs (libh);
    array = g_ptr_array_new ();
    dm_list_iterate_items(pvlist,pvs)
    {
        name = lvm_pv_get_name(pvlist->pv);
        g_ptr_array_add (array, g_strdup (name));
    }  
    lvm_list_pvs_free (pvs);
    dm_list_iterate_items(vglist, vgnames)
    {
        name = vglist->str;
        ret = shell_cmd_remove_vg (name);
    }
    for (i = 0; i < array->len; i++)
    { 
        name = g_ptr_array_index (array,i);
        shell_cmd_remove_pv (name); 
    }
    g_ptr_array_free (array, TRUE);
EXITVG:
    lvm_quit(libh);
    sysbak_gdbus_complete_remove_all_vg (object,invocation,ret);
    return ret;
}    
