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
 * egg_str_list_add_strv:
 **/
void
egg_str_list_add_strv (EggStrList *list, gchar **data)
{
	guint i;
	guint len;
	len = g_strv_length (data);
	for (i=0; i<len; i++)
		g_ptr_array_add (list, g_strdup (data[i]));
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
 * egg_str_list_remove:
 **/
gboolean
egg_str_list_remove (EggStrList *list, const gchar *data)
{
	guint i;
	gchar *data_tmp;
	gboolean ret = FALSE;

	for (i=0; i<list->len; i++) {
		data_tmp = (gchar *) g_ptr_array_index (list, i);
		if (egg_strequal (data, data_tmp)) {
			g_free (data_tmp);
			g_ptr_array_remove_index (list, i);
			ret = TRUE;
		}
	}
	return ret;
}

/**
 * egg_str_list_add_list:
 **/
void
egg_str_list_add_list (EggStrList *list, EggStrList *add)
{
	guint i;
	const gchar *data;
	for (i=0; i<add->len; i++) {
		data = egg_str_list_index (add, i);
		egg_str_list_add (list, data);
	}
}

/**
 * egg_str_list_remove_list:
 **/
void
egg_str_list_remove_list (EggStrList *list, EggStrList *remove)
{
	guint i;
	const gchar *data;
	for (i=0; i<remove->len; i++) {
		data = egg_str_list_index (remove, i);
		egg_str_list_remove (list, data);
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
	gchar *data1;
	const gchar *data2;

	for (i=0; i<list->len; i++) {
		data1 = (gchar *) g_ptr_array_index (list, i);
		for (j=0; j<list->len; j++) {
			if (i == j)
				break;
			data2 = egg_str_list_index (list, j);
			if (egg_strequal (data1, data2)) {
				g_free (data1);
				g_ptr_array_remove_index (list, i);
			}
		}
	}
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_string_list_test (EggTest *test)
{
	EggStrList *list;
	EggStrList *list2;

	if (!egg_test_start (test, "EggStrList"))
		return;

	/************************************************************/
	egg_test_title (test, "create new list");
	list = egg_str_list_new ();
	egg_test_assert (test, list != NULL);

	/************************************************************/
	egg_test_title (test, "length zero");
	egg_test_assert (test, list->len == 0);

	/************************************************************/
	egg_test_title (test, "add stuff to list");
	egg_str_list_add (list, "dave");
	egg_str_list_add (list, "mark");
	egg_str_list_add (list, "foo");
	egg_str_list_add (list, "foo");
	egg_str_list_add (list, "bar");
	egg_test_assert (test, list->len == 5);

	/************************************************************/
	egg_test_title (test, "create second list");
	list2 = egg_str_list_new ();
	egg_str_list_add (list2, "mark");
	egg_test_assert (test, list2->len == 1);

	/************************************************************/
	egg_test_title (test, "append the lists");
	egg_str_list_add_list (list, list2);
	egg_test_assert (test, list->len == 6);

	/************************************************************/
	egg_test_title (test, "remove duplicates");
	egg_str_list_remove_duplicate (list);
	egg_test_assert (test, list->len == 4);

	/************************************************************/
	egg_test_title (test, "remove one list from another");
	egg_str_list_add_list (list, list2); //dave,mark,foo,bar,mark
	egg_str_list_remove_list (list, list2);
	egg_test_assert (test, list->len == 3);

	egg_str_list_free (list2);
	egg_str_list_free (list);
	egg_test_end (test);
}
#endif

