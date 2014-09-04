/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>
#include <unistd.h>
#include <stdio.h>

#include <gpk-debug.h>

#include "config.h"

static gboolean _verbose = FALSE;
static gboolean _console = FALSE;

/**
 * gpk_debug_ignore_cb:
 **/
static void
gpk_debug_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		    const gchar *message, gpointer user_data)
{
}

#define CONSOLE_RESET		0
#define CONSOLE_BLACK		30
#define CONSOLE_RED		31
#define CONSOLE_GREEN		32
#define CONSOLE_YELLOW		33
#define CONSOLE_BLUE		34
#define CONSOLE_MAGENTA		35
#define CONSOLE_CYAN		36
#define CONSOLE_WHITE		37

#define GPK_DEBUG_LOG_DOMAIN_LENGTH	20

/**
 * gpk_debug_handler_cb:
 **/
static void
gpk_debug_handler_cb (const gchar *log_domain, GLogLevelFlags log_level,
		     const gchar *message, gpointer user_data)
{
	gchar str_time[255];
	time_t the_time;
	guint len;
	guint i;

	/* time header */
	time (&the_time);
	strftime (str_time, 254, "%H:%M:%S", localtime (&the_time));

	/* no color please, we're British */
	if (!_console) {
		if (log_level == G_LOG_LEVEL_DEBUG) {
			g_print ("%s\t%s\t%s\n", str_time, log_domain, message);
		} else {
			g_print ("***\n%s\t%s\t%s\n***\n", str_time, log_domain, message);
		}
		return;
	}

	/* time in green */
	g_print ("%c[%dm%s\t", 0x1B, CONSOLE_GREEN, str_time);

	/* log domain in either blue */
	if (g_strcmp0 (log_domain, G_LOG_DOMAIN) == 0)
		g_print ("%c[%dm%s%c[%dm", 0x1B, CONSOLE_BLUE, log_domain, 0x1B, CONSOLE_RESET);
	else
		g_print ("%c[%dm%s%c[%dm", 0x1B, CONSOLE_CYAN, log_domain, 0x1B, CONSOLE_RESET);

	/* pad with spaces */
	len = strlen (log_domain);
	for (i=len; i<GPK_DEBUG_LOG_DOMAIN_LENGTH; i++)
		g_print (" ");

	/* critical is also in red */
	if (log_level == G_LOG_LEVEL_CRITICAL ||
	    log_level == G_LOG_LEVEL_ERROR) {
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, CONSOLE_RED, message, 0x1B, CONSOLE_RESET);
	} else {
		/* debug in blue */
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, CONSOLE_BLUE, message, 0x1B, CONSOLE_RESET);
	}
}

/**
 * gpk_debug_pre_parse_hook:
 */
static gboolean
gpk_debug_pre_parse_hook (GOptionContext *context, GOptionGroup *group, gpointer data, GError **error)
{
	const GOptionEntry main_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &_verbose,
		  /* TRANSLATORS: turn on all debugging */
		  N_("Show debugging information for all files"), NULL },
		{ NULL}
	};

	/* add main entry */
	g_option_context_add_main_entries (context, main_entries, NULL);
	return TRUE;
}

/**
 * gpk_debug_add_log_domain:
 */
void
gpk_debug_add_log_domain (const gchar *log_domain)
{
	if (_verbose) {
		g_log_set_fatal_mask (log_domain, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
		g_log_set_handler (log_domain,
				   G_LOG_LEVEL_ERROR |
				   G_LOG_LEVEL_CRITICAL |
				   G_LOG_LEVEL_DEBUG |
				   G_LOG_LEVEL_WARNING,
				   gpk_debug_handler_cb, NULL);
	} else {
		/* hide all debugging */
		g_log_set_handler (log_domain,
				   G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_WARNING,
				   gpk_debug_ignore_cb, NULL);
	}
}

/**
 * gpk_debug_post_parse_hook:
 */
static gboolean
gpk_debug_post_parse_hook (GOptionContext *context, GOptionGroup *group, gpointer data, GError **error)
{
	/* verbose? */
	gpk_debug_add_log_domain (G_LOG_DOMAIN);
	_console = (isatty (fileno (stdout)) == 1);
	g_debug ("Verbose debugging %s (on console %i)", _verbose ? "enabled" : "disabled", _console);
	return TRUE;
}

/**
 * gpk_debug_get_option_group:
 *
 * Returns a #GOptionGroup for the command line arguments recognized
 * by debugging. You should add this group to your #GOptionContext
 * with g_option_context_add_group(), if you are using
 * g_option_context_parse() to parse your command line arguments.
 *
 * Returns: a #GOptionGroup for the command line arguments
 */
GOptionGroup *
gpk_debug_get_option_group (void)
{
	GOptionGroup *group;
	group = g_option_group_new ("debug", _("Debugging Options"), _("Show debugging options"), NULL, NULL);
	g_option_group_set_parse_hooks (group, gpk_debug_pre_parse_hook, gpk_debug_post_parse_hook);
	return group;
}

