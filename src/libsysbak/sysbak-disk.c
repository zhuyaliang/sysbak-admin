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
#include "sysbak-admin-generated.h"

#define   CMDOPTION   "NAME,FSTYPE,UUID";

static gboolean check_disk_device (const char *path)
{
    if (access (path,F_OK) == -1)
    {
        return FALSE;
    }    
    
    return TRUE;
}    
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
                          "TYPE",
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
    
    if (!check_disk_device (disk_name))
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