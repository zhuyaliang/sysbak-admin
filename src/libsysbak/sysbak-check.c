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

#include "sysbak-check.h"

gboolean check_device_mount(const char* device)
{
    char *real_file = NULL, *real_fsname = NULL;
    FILE * f;
    struct mntent * mnt;
    gboolean isMounted = FALSE;

    real_file = malloc(PATH_MAX + 1);
    if (!real_file) 
    {
        goto EXIT;
    }
    real_fsname = malloc(PATH_MAX + 1);
    if (!real_fsname) 
    {
        goto EXIT;
    }

    if (!realpath(device, real_file)) 
    {
        goto EXIT;
    }
    if ((f = setmntent(MOUNTED, "r")) == 0) 
    {
        goto EXIT;
    }
    while ((mnt = getmntent (f)) != 0) 
    {
        if (!realpath(mnt->mnt_fsname, real_fsname))
            continue;
        if (strcmp(real_file, real_fsname) == 0) 
        {
            isMounted = 1;
        }
    }
    endmntent(f);
EXIT:
    if (real_file)
    { 
        free(real_file); 
        real_file = NULL;
    }
    if (real_fsname)
    { 
        free(real_fsname); 
        real_fsname = NULL;
    }
    return isMounted;
}

gboolean check_file_permission (const char *path)
{
    if (access (path,R_OK) == -1)
    {
        return FALSE;
    }    
    
    return TRUE;
}    
gboolean check_file_device (const char *path)
{
    if (access (path,F_OK) == -1)
    {
        return FALSE;
    }    
    
    return TRUE;
}    
