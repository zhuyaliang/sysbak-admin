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
    sysbak_gdbus_emit_sysbak_finished (object,
                                       0,
                                       0,
                                       0);
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
    sysbak_gdbus_emit_sysbak_finished (object,
                                       0,
                                       0,
                                       0);
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
    sysbak_gdbus_emit_sysbak_finished (object,
                                       0,
                                       0,
                                       0);
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
    
    g_print ("s = %s\r\n",s);
    if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, NULL, NULL, NULL, &standard_error, &status, &error))
        goto ERROR;

    if (!g_spawn_check_exit_status (status, &error))
        goto ERROR;

    sysbak_gdbus_complete_backup_lvm_meta (object,invocation,TRUE); 
    sysbak_gdbus_emit_sysbak_finished (object,
                                       0,
                                       0,
                                       0);
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
