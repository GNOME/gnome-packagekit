/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <glib.h>
#include <glib-object.h>
#include "egg-test.h"
#include "egg-debug.h"
#include "gpk-common.h"
#include "gpk-enum.h"
#include "gpk-task.h"

void egg_markdown_test (EggTest *test);
void egg_string_test (EggTest *test);
void gpk_dbus_test (EggTest *test);
void gpk_language_test (EggTest *test);
void gpk_modal_dialog_test (EggTest *test);
void gpk_client_test (EggTest *test);
void gpk_error_test (EggTest *test);

int
main (int argc, char **argv)
{
	EggTest *test;

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();
	gtk_init (&argc, &argv);
	test = egg_test_init ();
	egg_debug_init (&argc, &argv);

	/* tests go here */
	egg_markdown_test (test);
	egg_string_test (test);
	gpk_enum_test (test);
	gpk_common_test (test);
//	gpk_dbus_test (test);
	gpk_language_test (test);
	gpk_error_test (test);
//	gpk_client_test (test);
	gpk_modal_dialog_test (test);
	gpk_task_test (test);

	return egg_test_finish (test);
}

