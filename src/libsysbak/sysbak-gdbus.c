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
#include "io-generated.h"
#define ORG_NAME  "org.io.operation.gdbus"
#define DBS_NAME  "/org/io/operation/gdbus"

typedef struct
{
   gboolean	   overwrite;
   char       *source; 
   char       *target;
   IoGdbus    *proxy;
} SysbakAdminPrivate;
 
G_DEFINE_TYPE_WITH_PRIVATE (SysbakAdmin, sysbak_admin, G_TYPE_OBJECT)

static void     sysbak_admin_class_init (SysbakAdminClass *klass);
static void     sysbak_admin_init       (SysbakAdmin      *sysbak);
static void     sysbak_admin_finalize   (GObject          *object);


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
	priv->proxy = io_gdbus_proxy_new_sync (connection,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           ORG_NAME,
                                           DBS_NAME,
                                           NULL,
                                          &error);
	if (!priv->proxy)
	{
		g_warning ("proxy_new_sync failed %s\r\n",error->message);

	}
}
static void sysbak_admin_class_init (SysbakAdminClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = sysbak_admin_finalize;
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
	
	priv->source = g_strdup (target);
}

void sysbak_admin_set_option (SysbakAdmin *sysbak,gboolean overwrite)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	
	priv->overwrite = overwrite;
}

void sysbak_admin_finished_signal  (SysbakAdmin   *sysbak,
		                            finished_func  function,
									gpointer       user_data)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	IoGdbus *proxy;
	proxy = priv->proxy;
	g_signal_connect_object (proxy, 
			                "sysbak-finished", 
							 G_CALLBACK(function), 
							 user_data, 
							 G_CONNECT_SWAPPED);
}
void sysbak_admin_progress_signal  (SysbakAdmin   *sysbak,
		                            progress_func  function,
									gpointer       user_data)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	IoGdbus *proxy;
	proxy = priv->proxy;
	g_signal_connect_object (proxy, 
			                "sysbak-progress", 
							 G_CALLBACK(function), 
							 user_data, 
							 G_CONNECT_SWAPPED);
}	
void sysbak_admin_error_signal  (SysbakAdmin   *sysbak,
		                         error_func     function,
								 gpointer       user_data)
{
	SysbakAdminPrivate *priv = sysbak_admin_get_instance_private (sysbak);
	IoGdbus *proxy;
	proxy = priv->proxy;
	g_signal_connect_object (proxy, 
			                "sysbak-error", 
							 G_CALLBACK(function), 
							 user_data, 
							 G_CONNECT_SWAPPED);
}	
SysbakAdmin *sysbak_admin_new (void)
{
	return g_object_new (SYSBAK_TYPE_ADMIN,NULL);
}
