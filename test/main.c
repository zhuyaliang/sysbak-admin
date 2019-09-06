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

#include <libsysbak/sysbak-gdbus.h>
#include <libsysbak/sysbak-extfs.h>

#define   TESTCONFIG     "./test.ini"
#define   TESTMAX         3

typedef void (*test_func) (SysbakAdmin *);

typedef struct 
{
    char *test;
    int   value;
    int   key;
}call_data;

typedef struct 
{
    char      *enable_key;
    test_func  start_test;
}test_data;

void call_progress (progress_data *data,gpointer p_data)
{

    printf ("%s Percentile %.2f speed %.2f KB/S\r\n",(const char *)p_data,data->percent,data->speed);
}

static void start_test_ptf (SysbakAdmin *sysbak)
{
    g_autoptr(GError) error = NULL;

    if (!sysbak_admin_extfs_ptf_async (sysbak,&error))
    {
        g_print ("sysbak_admin_extfs_ptf_async failed %s\r\n",error->message);
    }    
}    

static void start_test_ptp (SysbakAdmin *sysbak)
{
    g_autoptr(GError) error = NULL;
	if (!sysbak_admin_extfs_ptp_async (sysbak,&error))
    {
        g_print ("sysbak_admin_extfs_ptp_async failed %s\r\n",error->message);
    }    
}    

static void start_test_restore (SysbakAdmin *sysbak)
{
    g_autoptr(GError) error = NULL;

	if (!sysbak_admin_extfs_restore_async (sysbak,&error))
    {
        g_print ("sysbak_admin_extfs_restore_async failed %s\r\n",error->message);
    }    
} 

void finished_cb (SysbakAdmin   *sysbak,
                  finished_data *fdata,
                  gpointer       d)
{
    call_data *data = (call_data *)d;
    
    g_print ("\r\ntotalblock = %lu\ 
              usedblocks = %lu\
              block_size = %u\
              test = %s\r\n",fdata->totalblock,fdata->usedblocks,fdata->block_size,data->test);
}
void progress_cb (SysbakAdmin   *sysbak,
                  progress_data *pdata,
                  gpointer       d)
{
    call_data *data = (call_data *)d;

    g_print ("\r percent %.2f speed %.2f elapsed  %2lu test %s",
                 pdata->percent,pdata->speed,pdata->elapsed,data->test);
}

void error_cb (SysbakAdmin *sysbak,
               const char  *error_message,
               gpointer     data)
{
    g_print ("error->message = %s\r\n",error_message);
}
void set_call_data (call_data *pdata)
{
    pdata->test = g_strdup ("hello world");
    pdata->value = 10;
    pdata->key = 9;
}    
int main(int argc, char **argv)
{
    GMainLoop    *loop;
	SysbakAdmin  *sysbak;
    call_data     pdata;
	GKeyFile     *kconfig = NULL;
    GError       *error = NULL;
    g_auto(GStrv) test_groups = NULL;
    gsize         length = 0;
    gint          value = 0;
    gint          overwrite = 0;
    char         *source,*targer; 
    int           mode = 0;
    
    static test_data array_test_data [TESTMAX] = 
    {
        {"enable_ptp",    start_test_ptp},
        {"enable_ptf",    start_test_ptf},
        {"enable_restore",start_test_restore}
    };
	
    loop = g_main_loop_new (NULL, FALSE);
	sysbak = sysbak_admin_new ();
    set_call_data (&pdata);        
    g_signal_connect(sysbak, 
                    "signal-progress", 
                     G_CALLBACK(progress_cb),
                     &pdata);

    g_signal_connect(sysbak, 
                    "signal-error",    
                     G_CALLBACK(error_cb), 
                     loop);

    g_signal_connect(sysbak, 
                    "signal-finished", 
                     G_CALLBACK(finished_cb),
                     &pdata);

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
        overwrite = g_key_file_get_integer(kconfig,
                                           test_groups[i],
                                           "overwrite",
                                           &error);
        g_print ("source = %s targer = %s overwrite = %d\r\n",source,targer,overwrite);
	    sysbak_admin_set_source (sysbak,source);
	    sysbak_admin_set_target (sysbak,targer);
	    sysbak_admin_set_option (sysbak,overwrite);

        array_test_data[i].start_test(sysbak);
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
