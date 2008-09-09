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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#include "config.h"
#include <glib.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-string-list.h"

/**
 * egg_str_list_new:
 **/
inline EggStrList *
egg_str_list_new (void)
{
	return g_ptr_array_new ();
}

/**
 * egg_str_list_free:
 **/
inline void
egg_str_list_free (EggStrList *list)
{
	g_ptr_array_foreach (list, (GFunc) g_free, NULL);
	g_ptr_array_free (list, TRUE);
}

/**
 * egg_str_list_add:
 **/
inline void
egg_str_list_add (EggStrList *list, const gchar *data)
{
	g_ptr_array_add (list, g_strdup (data));
}

/**
 * egg_str_list_index:
 **/
inline const gchar *
egg_str_list_index (EggStrList *list, guint index)
{
	return (const gchar *) g_ptr_array_index (list, index);
}

/**
 * egg_str_list_print:
 **/
void
egg_str_list_print (EggStrList *list)
{
	guint i;
	const gchar *data;

	for (i=0; i<list->len; i++) {
		data = egg_str_list_index (list, i);
		egg_debug ("list[%i] = %s", i, data);
	}
}

/**
 * egg_str_list_remove_duplicate:
 * @array: A GPtrArray instance
 **/
void
egg_str_list_remove_duplicate (EggStrList *list)
{
	guint i, j;
	const gchar *data1;
	const gchar *data2;

	for (i=0; i<list->len; i++) {
		data1 = egg_str_list_index (list, i);
		for (j=0; j<list->len; j++) {
			if (i == j)
				break;
			data2 = egg_str_list_index (list, j);
			if (egg_strequal (data1, data2))
				g_ptr_array_remove_index (list, i);
		}
	}
}

