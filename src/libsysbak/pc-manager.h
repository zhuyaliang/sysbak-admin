/* vi: set sw=4 ts=4 wrap ai: */
/*
 * pc-manager.h: This file is part of ____
 *
 * Copyright (C) 2019 yetist <yetist@yetibook>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * */

#ifndef __PC_MANAGER_H__
#define __PC_MANAGER_H__  1

#include <glib-object.h>

G_BEGIN_DECLS

#define PC_TYPE_MANAGER  (pc_manager_get_type ())
G_DECLARE_DERIVABLE_TYPE (PCManager, pc_manager, PC, MANAGER, GObject)

struct _PCManagerClass
{
    GObjectClass   parent_class;
    void           (* error)       (PCManager *manager, const char *message);
};

PCManager*     pc_manager_new                (void);
void           pc_manager_set_source         (PCManager *manager, const gchar *source);
void           pc_manager_set_target         (PCManager *manager, const gchar *target);
void           pc_manager_set_option         (PCManager *manager, const gchar *option, GError **error);
gboolean       pc_manager_prepare            (PCManager *manager, GError **error);
void           pc_manager_clone              (PCManager *manager);
void           pc_manager_clone_async        (PCManager *manager);

G_END_DECLS

#endif /* __PC_MANAGER_H__ */
