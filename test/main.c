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
#include <stdlib.h>
#include <unistd.h>

#include <libsysbak/sysbak-extfs.h>

#define   TESTCONFIG     "./test.ini"
#define   TESTMAX         3

typedef void (*test_func) (char *,char *);
typedef struct 
{
    char      *enable_key;
    test_func  start_test;
}test_data;

static void call_finished (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      f_data)
{
    device_info *dev_info;
    GError *error = NULL;

    dev_info = sysbak_extfs_finish (res,&error);
    if (dev_info == NULL)
    {
        g_print ("%s Faild %s\r\n",(const char *)f_data,error->message);
    }
    else
    {
        g_print ("file system type:  Successful %s\r\n",dev_info->fs);
    }
}    

void call_progress (progress_data *data,gpointer p_data)
{

    printf ("%s Percentile %.2f speed %.2f KB/S\r\n",(const char *)p_data,data->percent,data->speed);
}

static void start_test_ptf (char *source,char *targer)
{
    GCancellable *cancellable;
    const char *data = "ptf";

    cancellable = g_cancellable_new ();
    g_print ("start Partition to File Backup \r\n");
    if (!sysbak_extfs_ptf_async ((const char *)source, 
                                 (const char *)targer,
                                  0,
                                  cancellable,
                                  call_finished,
                                 (gpointer)data,
                                  call_progress,
                                 (gpointer)data))
    {
        g_print ("Partition-to-File backup failed \r\n");
    } 
    free (source);
    free (targer);
    g_object_unref (cancellable);
}    

static void start_test_ptp (char *source,char *targer)
{
    GCancellable *cancellable;
    const char *data = "ptp";

    cancellable = g_cancellable_new ();
    g_print ("start Partition to Partition Backup \r\n");

    if (!sysbak_extfs_ptp_async ((const char *)source, 
                                 (const char *)targer,
                                  0,
                                  cancellable,
                                  call_finished,
                                 (gpointer)data,
                                  call_progress,
                                 (gpointer)data))
    {
        g_print ("Partition-to-partition backup failed \r\n");
    } 
    free (source);
    free (targer);
    g_object_unref (cancellable);
}    

static void start_test_restore (char *source,char *targer)
{
    GCancellable *cancellable;
    const char *data = "restore";

    cancellable = g_cancellable_new ();
    g_print ("start resatore backup from files \r\n");
    if (!sysbak_extfs_restore_async ((const char *)source, 
                                     (const char *)targer,
                                      0,
                                      cancellable,
                                      call_finished,
                                     (gpointer)data,
                                      call_progress,
                                     (gpointer)data))
    {
        g_print ("resatore backup from files failed \r\n");
    } 
    free (source);
    free (targer);
    g_object_unref (cancellable);
}    
int main(int argc, char **argv)
{
    GKeyFile     *kconfig = NULL;
    GMainLoop    *loop;
    GError       *error = NULL;
    g_auto(GStrv) test_groups = NULL;
    gsize         length = 0;
    gint          value = 0;
    char         *source,*targer; 
    int           mode = 0;

    static test_data array_test_data [TESTMAX] = 
    {
        {"enable_ptp",    start_test_ptp},
        {"enable_ptf",    start_test_ptf},
        {"enable_restore",start_test_restore}
    };

    loop = g_main_loop_new (NULL, FALSE);
    kconfig = g_key_file_new();
    if(kconfig == NULL)
    {
        g_print ("test exit g_key_file_new Error\r\n");
        goto ERROR;
    }
    if(!g_key_file_load_from_file(kconfig, TESTCONFIG, G_KEY_FILE_NONE, &error))
    {
        g_print ("test exit %s\r\n",error->message);
        goto ERROR;
    }
    test_groups = g_key_file_get_groups(kconfig, &length);
    if(g_strv_length(test_groups) <= 0)
    {
        g_print ("test exit %s\r\n","test group is empty");
        goto ERROR;
    }
    for (guint i = 0; i < length; i++)
    {
        value = g_key_file_get_integer(kconfig,
                                       test_groups[i],
                                       array_test_data[i].enable_key,
                                       &error);
        if (value != 1)
        {
            continue;
        }    
        source = g_key_file_get_string(kconfig,
                                       test_groups[i],
                                      "source",
                                      &error);
        if (source == NULL)
        {
            g_warning ("test group %s key source no setting\r\n",test_groups[i]);
            continue;
        }    
        targer = g_key_file_get_string(kconfig,
                                       test_groups[i],
                                      "targer",
                                      &error);
        if (targer == NULL)
        {
            g_warning ("test group %s key targer no setting\r\n",test_groups[i]);
            free (source);
            continue;
        }   
        g_print ("source = %s targer = %s\r\n",source,targer);
        array_test_data[i].start_test(source,targer);
        mode++;
    }    

    if (mode == 0 )
    {
        g_print ("You haven't done anything. Please configure the test.ini file to open the test options.\r\n");
    }    
    g_main_loop_run (loop);
ERROR:
    if (kconfig != NULL)
    {
        g_key_file_free (kconfig);
    }    
    g_main_loop_unref (loop);
    g_main_loop_quit (loop);
}    
