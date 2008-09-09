/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __EGG_STRING_LIST_H
#define __EGG_STRING_LIST_H

#include <glib.h>

G_BEGIN_DECLS

typedef GPtrArray EggStrList;

EggStrList	*egg_str_list_new			(void);
void		 egg_str_list_free			(EggStrList	*list);
void		 egg_str_list_add			(EggStrList	*list,
							 const gchar	*data);
const gchar	*egg_str_list_index			(EggStrList	*list,
							 guint		 index);
void		 egg_str_list_print			(EggStrList	*list);
void		 egg_str_list_remove_duplicate		(EggStrList	*list);

G_END_DECLS

#endif /* __EGG_STRING_LIST_H */
