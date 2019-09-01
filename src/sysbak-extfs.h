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

#ifndef __SYSBAK_EXTFS_H__
#define __SYSBAK_EXTFS_H__

#include <glib.h>
#include <gio/gio.h>
#include <sysbak-share.h>

//Backup partition to image file 
    
gboolean sysbak_extfs_ptf_async (const char   *device,
                                 const char   *targer,
                                 gboolean      overwrite,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback finished_callback,
                                 gpointer      f_data,
                                 sysbak_progress progress_callback,
                                 gpointer      p_data);
file_system_info *sysbak_extfs_finish (GAsyncResult  *result,
                                           GError       **error); 
//Backup partition to partition 

gboolean sysbak_extfs_ptp_async (const char   *device,
                                 const char   *targer,
                                 gboolean      overwrite,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback finished_callback,
                                 gpointer      f_data,
                                 sysbak_progress progress_callback,
                                 gpointer      p_data);

gboolean sysbak_extfs_restore_async (const char   *device,
                                     const char   *targer,
                                     gboolean      overwrite,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback finished_callback,
                                     gpointer      f_data,
                                     sysbak_progress progress_callback,
                                     gpointer      p_data);
#endif
