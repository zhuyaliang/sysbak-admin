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

#include "sysbak-extfs.h"
#include "sysbak-share.h"
#include "progress.h"

typedef struct 
{
    // Source Data File Descriptor
    int     dfr;   
    char   *source;
    // Tagrger Data File Descriptor
    int     dfw;    
    char   *target;
    sysbak_progress progress_callback;
    gpointer p_data;
    gboolean overwrite;
    GCancellable *cancellable;
}sysdata;

static progress_bar prog;
static gboolean loop_check_progress (gpointer d)
{
    sysdata *data = (sysdata *)d;
    progress_data progressdata;
    ull copied_count;

    //copied_count = libgdbus_get_extfs_read_size ();
    g_print ("copied_count = %llu\r\n",copied_count);
    if (g_cancellable_is_cancelled (data->cancellable))
    {
        return FALSE;
    }
    if (!progress_update(&prog, copied_count,&progressdata))
    {
        progressdata.percent=100.0;
        progressdata.remained = (time_t)0;
        data->progress_callback (&progressdata,data->p_data);
        return FALSE;
    }
    data->progress_callback (&progressdata,data->p_data);
    return TRUE;
}
static void load_progress_info (ull      usedblocks,
								uint     block_size,
                                sysdata *data)
{
    g_return_if_fail (data->progress_callback != NULL);
    
	progress_init(&prog, 0, usedblocks, block_size);
    g_timeout_add(800,(GSourceFunc)loop_check_progress,data);
}
static void sys_data_free (sysdata *data)
{
    free(data->source);
    free(data->target);
    g_slice_free (sysdata,data);
}
static void destory_dev_info (device_info *dev_info)
{
    g_slice_free(device_info,dev_info);
}    

static void start_sysbak_data_ptf (GTask         *task,
                                   gpointer       source_object,
                                   gpointer       d,
                                   GCancellable  *cancellable)
{
    sysdata *data = (sysdata *)d;
    device_info     *dev_info;
    GError          *error = NULL;

	dev_info = get_extfs_device_info (data->source);
    if (dev_info == NULL)
    {
        goto ERROR;
    }    
    load_progress_info (dev_info->usedblocks,
						dev_info->block_size,
                        data);

    if (!libgdbus_sysbak_extfs_ptf (data->source,
                                    data->target,
                                    data->overwrite,
                                    &error))
    {
        goto ERROR;
    } 

    g_task_return_pointer (task,dev_info,(GDestroyNotify)destory_dev_info);
    return;
ERROR:
	destory_dev_info(dev_info);
    g_cancellable_cancel (cancellable);
    g_task_return_error (task, error);

}   
static void start_sysbak_data_ptp (GTask         *task,
                                   gpointer       source_object,
                                   gpointer       d,
                                   GCancellable  *cancellable)
{
    sysdata *data = (sysdata *)d;
    device_info     *dev_info;
    GError          *error = NULL;

	dev_info = get_extfs_device_info (data->source);
    if (dev_info == NULL)
    {
        goto ERROR;
    }    
    load_progress_info (dev_info->usedblocks,
						dev_info->block_size,
                        data);
    if (!gdbus_sysbak_extfs_ptp (data->source,
                                 data->target,
                                 data->overwrite,
                                 &error))
    {
        goto ERROR;
    }

    g_task_return_pointer (task,dev_info,(GDestroyNotify)destory_dev_info);
    return;
ERROR:
	destory_dev_info(dev_info);
    g_cancellable_cancel (cancellable);
    g_task_return_error (task, error);

}   

static void start_sysbak_data_restore (GTask         *task,
									   gpointer       source_object,
                                       gpointer       d,
                                       GCancellable  *cancellable)
{
    sysdata *data = (sysdata *)d;
    device_info     *dev_info;
    GError          *error = NULL;

	dev_info = get_extfs_image_info (data->source);
    if (dev_info == NULL)
    {
        goto ERROR;
    }   
    /*
    load_progress_info (dev_info->usedblocks,
						dev_info->block_size,
                        data);
*/
    g_print ("dev_info->usedblocks = %llu \r\n",dev_info->usedblocks);
    if (!libgdbus_sysbak_extfs_restore (data->source,data->target,data->overwrite,&error))
    {
        g_print ("error=>message = %s \r\n",error->message);
        goto ERROR;
    }

    g_task_return_pointer (task,dev_info,(GDestroyNotify)destory_dev_info);
    return;
ERROR:
	destory_dev_info(dev_info);
    g_cancellable_cancel (cancellable);
    g_task_return_error (task, error);

}   
//Backup partition to image file 
/******************************************************************************
 * Function:              sysbak_extfs_ptf      
 *        
 * Explain: Backup partition to image file eg: /dev/sda1 ---> sda1.img
 *        
 * Input:   @device       Partitions to be backed up
 *          @targer       Name after backup
 *          @overwrite    Overwrite existing backups
 *          @finished_ca  Callback function after successful backup
 *          @f_data       function data;
 *          @progress_ca  Backup process callback function
 *          @p_data       function data;
 * Output:  success      :TRUE
 *          fail         :FALSE
 *        
 * Author:  zhuyaliang  29/08/2019
 ******************************************************************************/
gboolean sysbak_extfs_ptf_async (const char   *source,
                                 const char   *target,
                                 gboolean      overwrite,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback finished_callback,
                                 gpointer      f_data,
                                 sysbak_progress progress_callback,
                                 gpointer      p_data)
{
    GTask *task;
    sysdata *data;

    g_return_val_if_fail (source != NULL,FALSE);
    g_return_val_if_fail (target != NULL,FALSE);
    g_return_val_if_fail (finished_callback != NULL,FALSE);

    if (cancellable == NULL)
    {
        cancellable = g_cancellable_new (); 
    }
    data = g_slice_new (sysdata);
    data->cancellable = cancellable;
    data->source = g_strdup (source);
    data->target = g_strdup (target);
    data->progress_callback = progress_callback;
    data->p_data = p_data;

    task = g_task_new (NULL,cancellable,finished_callback,f_data);
    g_task_set_task_data (task, data, (GDestroyNotify) sys_data_free);
    g_task_run_in_thread (task, start_sysbak_data_ptf);
    g_object_unref (task);
    return TRUE;      /// finish
}
//Backup partition to partition 
/******************************************************************************
 * Function:              sysbak_extfs_ptp      
 *        
 * Explain: Backup partition to image file eg: /dev/sda1 ---> /dev/sdc1
 *        
 * Input:   @device       Partitions to be backed up
 *          @targer       Name after backup
 *          @overwrite    Overwrite existing backups
 *          @finished_ca  Callback function after successful backup
 *          @f_data       function data;
 *          @progress_ca  Backup process callback function
 *          @p_data       function data;
 * Output:  success      :TRUE
 *          fail         :FALSE
 *        
 * Author:  zhuyaliang  29/08/2019
 ******************************************************************************/
gboolean sysbak_extfs_ptp_async (const char   *source,
                                 const char   *target,
                                 gboolean      overwrite,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback finished_callback,
                                 gpointer      f_data,
                                 sysbak_progress progress_callback,
                                 gpointer      p_data)
{
    GTask *task;
    sysdata *data;

    g_return_val_if_fail (source != NULL,FALSE);
    g_return_val_if_fail (target != NULL,FALSE);
    g_return_val_if_fail (finished_callback != NULL,FALSE);

    if (cancellable == NULL)
    {
        cancellable = g_cancellable_new (); 
    }
    data = g_slice_new (sysdata);
    data->cancellable = cancellable;
    data->source = g_strdup (source);
    data->target = g_strdup (target);
    data->progress_callback = progress_callback;
    data->p_data = p_data;

    task = g_task_new (NULL,cancellable,finished_callback,f_data);
    g_task_set_task_data (task, data, (GDestroyNotify) sys_data_free);
    g_task_run_in_thread (task, start_sysbak_data_ptp);
    g_object_unref (task);

    return TRUE;      /// finish
}
/******************************************************************************
 * Function:              sysbak_extfs_restore      
 *        
 * Explain: Restore backup data eg: sda1.img ----> /dev/sda1
 *        
 * Input:   @device       image name  sda1.img
 *          @targer       device name /dev/sda1
 *          @overwrite    Overwrite existing restore
 *          @finished_ca  Callback function after successful restore
 *          @f_data       function data;
 *          @progress_ca  restore process callback function
 *          @p_data       function data;
 * Output:  success      :TRUE
 *          fail         :FALSE
 *        
 * Author:  zhuyaliang  29/08/2019
 ******************************************************************************/
gboolean sysbak_extfs_restore_async (const char   *source,
                                     const char   *target,
                                     gboolean      overwrite,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback finished_callback,
                                     gpointer      f_data,
                                     sysbak_progress progress_callback,
                                     gpointer      p_data)
{
    GTask *task;
    sysdata *data;

    g_return_val_if_fail (source != NULL,FALSE);
    g_return_val_if_fail (target != NULL,FALSE);
    g_return_val_if_fail (finished_callback != NULL,FALSE);

    if (cancellable == NULL)
    {
        cancellable = g_cancellable_new (); 
    }
    data = g_slice_new (sysdata);
    data->progress_callback = progress_callback;
    data->p_data = p_data;
    data->cancellable = cancellable;
    data->source = g_strdup (source);
    data->target = g_strdup (target);
    data->overwrite = overwrite;
    task = g_task_new (NULL,cancellable,finished_callback,f_data);
    g_task_set_task_data (task, data, (GDestroyNotify) sys_data_free);
    g_task_run_in_thread (task, start_sysbak_data_restore);
    g_object_unref (task);

    return TRUE;      /// finish
}
device_info *sysbak_extfs_finish (GAsyncResult  *result,
                                  GError       **error) 
{
    device_info *dev_info;
    g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    dev_info = g_task_propagate_pointer (G_TASK (result), error);

    return dev_info;
}
