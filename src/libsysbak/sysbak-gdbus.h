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

#ifndef __SYSBAK_GDBUS_H__
#define __SYSBAK_GDBUS_H__

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define SYSBAK_TYPE_ADMIN         (sysbak_admin_get_type ())
#define SYSBAK_ADMIN(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), SYSBAK_TYPE_ADMIN, SysbakAdmin))
#define SYSBAK_ADMIN_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), SYSBAK_TYPE_ADMIN, SysbakAdminClass))
#define IS_SYSBAK_ADMIN(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), SYSBAK_TYPE_ADMIN))
#define IS_SYSBAK_ADMIN_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), SYSBAK_TYPE_ADMIN))
#define SYSBAK_ADMIN_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), SYSBAK_TYPE_ADMIN, SysbakAdminClass))


typedef struct  
{
    GObject   parent;
}SysbakAdmin;
typedef struct  
{
    GObjectClass   parent_class;
}SysbakAdminClass;

typedef void (*finished_func) (guint64,guint64,uint,gpointer);
typedef void (*progress_func) (double,double,guint64, guint64,gpointer);
typedef void (*error_func)    (int, const char*,gpointer);

GType            sysbak_admin_get_type         (void) G_GNUC_CONST;

SysbakAdmin     *sysbak_admin_new              (void);

const char      *sysbak_admin_get_source       (SysbakAdmin    *sysbak);

const char      *sysbak_admin_get_target       (SysbakAdmin    *sysbak);

gboolean         sysbak_admin_get_option       (SysbakAdmin    *sysbak);

gpointer         sysbak_admin_get_proxy        (SysbakAdmin    *sysbak);

void             sysbak_admin_set_source       (SysbakAdmin    *sysbak,
		                                        const char     *source);

void             sysbak_admin_set_target       (SysbakAdmin    *sysbak,
		                                        const char     *target);

void             sysbak_admin_set_option       (SysbakAdmin    *sysbak,
		                                        gboolean       overwrite);

void             sysbak_admin_finished_signal  (SysbakAdmin   *sysbak,
		                                        finished_func  function,
												gpointer       user_data);

void             sysbak_admin_progress_signal  (SysbakAdmin   *sysbak,
		                                        progress_func  function,
												gpointer       user_data);

void             sysbak_admin_error_signal     (SysbakAdmin   *sysbak,
		                                        error_func     function,
												gpointer       user_data);
G_END_DECLS
#endif
