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

#include "sysbak-gdbus.h"
#include "sysbak-admin-generated.h"
#define ORG_NAME  "org.sysbak.admin.gdbus"
#define DBS_NAME  "/org/sysbak/admin/gdbus"

enum 
{
    SIGNAL_ERROR,
    SIGNAL_PROGRESS,
    SIGNAL_FINISHED,
    LAST_SIGNAL
};
typedef struct
{
   gboolean	       overwrite;
   char           *source; 
   char           *target;
   SysbakGdbus    *proxy;
} SysbakAdminPrivate;
static guint signals[LAST_SIGNAL] = { 0 }; 
G_DEFINE_TYPE_WITH_PRIVATE (SysbakAdmin, sysbak_admin, G_TYPE_OBJECT)

static void on_progress (SysbakAdmin *sysbak,
                         double       percent,
                         double       speed,
                         guint64      elapsed)
{
    progress_data pdata;
    pdata.percent = percent;
    pdata.speed = speed;
    pdata.elapsed = elapsed;

    g_signal_emit (sysbak, 
                   signals[SIGNAL_PROGRESS], 
                   0,
                   &pdata);
}    

static void on_findshed (SysbakAdmin *sysbak,
                         guint64      totalblock,
                         guint64      usedblocks,
                         guint        block_size)
{
    finished_data fdata;

    fdata.totalblock = totalblock;
    fdata.usedblocks = usedblocks;
    fdata.block_size = block_size;

    g_signal_emit (sysbak, 
                   signals[SIGNAL_FINISHED], 
                   0,
                   &fdata);
}    
static void on_error (SysbakAdmin *sysbak,
                      const char  *error_message,
					  int          e_code)
{
    g_signal_emit (sysbak, 
                   signals[SIGNAL_ERROR], 
                   0,
                   error_message);

}    
static void sysbak_admin_finalize  (GObject *object)
{
	SysbakAdmin *sysbak = SYSBAK_ADMIN (object);
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	
	g_free (priv->source);
	g_free (priv->target);
}
static void sysbak_admin_init (SysbakAdmin *sysbak)
{
	GDBusConnection *connection;
	g_autoptr(GError) error = NULL;

	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
    connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (connection == NULL)
    {
		g_warning ("g_bus_get_sync failed %s\r\n",error->message);
        return;
    }
	priv->proxy = sysbak_gdbus_proxy_new_sync (connection,
                                               G_DBUS_PROXY_FLAGS_NONE,
                                               ORG_NAME,
                                               DBS_NAME,
                                               NULL,
                                              &error);
	if (!priv->proxy)
	{
		g_warning ("proxy_new_sync failed %s\r\n",error->message);

	}
	g_signal_connect_object (priv->proxy, 
			                "sysbak-progress", 
							 G_CALLBACK(on_progress), 
							 sysbak, 
							 G_CONNECT_SWAPPED);
	
    g_signal_connect_object (priv->proxy, 
			                "sysbak-finished", 
							 G_CALLBACK(on_findshed), 
							 sysbak, 
							 G_CONNECT_SWAPPED);

	g_signal_connect_object (priv->proxy, 
			                "sysbak-error", 
							 G_CALLBACK(on_error), 
							 sysbak, 
							 G_CONNECT_SWAPPED);

}
static void sysbak_admin_class_init (SysbakAdminClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = sysbak_admin_finalize;
    
    signals [SIGNAL_ERROR] =
        g_signal_new ("signal-error",
                      G_TYPE_FROM_CLASS (object_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SysbakAdminClass, signal_error),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE,
                      1, G_TYPE_STRING);
    signals [SIGNAL_FINISHED] =
        g_signal_new ("signal-finished",
                      G_TYPE_FROM_CLASS (object_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SysbakAdminClass, signal_finished),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE,
                      1, G_TYPE_POINTER);
    signals [SIGNAL_PROGRESS] =
        g_signal_new ("signal-progress",
                      G_TYPE_FROM_CLASS (object_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SysbakAdminClass, signal_progress),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE,
                      1, G_TYPE_POINTER);
}

const char *sysbak_admin_get_source (SysbakAdmin *sysbak)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	
	return priv->source;
}
const char *sysbak_admin_get_target (SysbakAdmin *sysbak)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	
	return priv->target;
}

gboolean sysbak_admin_get_option (SysbakAdmin *sysbak)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	
	return priv->overwrite;
}

gpointer sysbak_admin_get_proxy (SysbakAdmin *sysbak)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	return priv->proxy;
}
void sysbak_admin_set_source (SysbakAdmin *sysbak,const char *source)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	
	priv->source = g_strdup (source);
}

void sysbak_admin_set_target (SysbakAdmin *sysbak,const char *target)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	
	priv->target = g_strdup (target);
}

void sysbak_admin_set_option (SysbakAdmin *sysbak,gboolean overwrite)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	
	priv->overwrite = overwrite;
}

SysbakAdmin *sysbak_admin_new (void)
{
	return g_object_new (SYSBAK_TYPE_ADMIN,NULL);
}
