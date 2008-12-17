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

#include "config.h"

#include <string.h>
#include <glib.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-markdown.h"

#define EGG_MARKDOWN_MAX_LINE_LENGTH	1024

/**
 * egg_markdown_to_text_line_is_rule:
 *
 * Horizontal rules are created by placing three or more hyphens, asterisks,
 * or underscores on a line by themselves.
 * You may use spaces between the hyphens or asterisks.
 **/
static gboolean
egg_markdown_to_text_line_is_rule (const gchar *line)
{
	guint i;
	guint len;
	guint count = 0;
	gchar *copy = NULL;
	gboolean ret = FALSE;

	len = egg_strlen (line, EGG_MARKDOWN_MAX_LINE_LENGTH);
	if (len == 0)
		goto out;

	/* replace non-rule chars with ~ */
	copy = g_strdup (line);
	g_strcanon (copy, "-*_ ", '~');
	for (i=0; i<len; i++) {
		if (copy[i] == '~')
			goto out;
		if (copy[i] != ' ')
			count++;
	}

	/* if we matched, return true */
	if (count >= 3)
		ret = TRUE;
out:
	g_free (copy);
	return ret;
}

/**
 * egg_markdown_to_text_line_pass1:
 *
 * All the things that are on one line and cannot be split across multiple
 * lines.
 **/
static gchar *
egg_markdown_to_text_line_pass1 (const gchar *line)
{
	gchar *copy;
	guint len;
	gchar *data = NULL;

	/* strip all spaces */
	copy = g_strdup (line);
	g_strstrip (copy);

	/* blank line */
	len = egg_strlen (copy, EGG_MARKDOWN_MAX_LINE_LENGTH);
	if (len == 0) {
		data = g_strdup_printf ("\n");
		goto out;
	}

	/* this is a rule */
	if (egg_markdown_to_text_line_is_rule (copy)) {
		data = g_strdup_printf ("\n⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯");
		goto out;
	}

	/* is this a list item (be a bit more forgiving and include '-') */
	if (g_str_has_prefix (copy, "- ") || g_str_has_prefix (copy, "* ")) {
		data = g_strdup_printf ("\n• %s", &copy[2]);
		goto out;
	}

	/* is this a H1 header */
	if (g_str_has_prefix (copy, "# ")) {
		data = g_strdup_printf ("\n<big>%s</big>\n", &copy[2]);
		goto out;
	}

	/* nothing recognised */
	data = g_strdup_printf ("%s ", copy);
out:
	g_free (copy);
	return data;
}

/**
 * egg_markdown_to_text_line_formatter:
 **/
static gchar *
egg_markdown_to_text_line_formatter (const gchar *line, const gchar *formatter, const gchar *left, const gchar *right)
{
	guint len;
	gchar *str1;
	gchar *str2;
	gchar *start = NULL;
	gchar *middle = NULL;
	gchar *end = NULL;
	gchar *copy = NULL;
	gchar *data = NULL;
	gchar *temp;

	/* needed to know for shifts */
	len = egg_strlen (formatter, EGG_MARKDOWN_MAX_LINE_LENGTH);
	if (len == 0)
		goto out;

	/* find sections */
	copy = g_strdup (line);
	str1 = strstr (copy, formatter);
	if (str1 != NULL) {
		*str1 = '\0';
		str2 = strstr (str1+1, formatter);
		if (str2 != NULL) {
			*str2 = '\0';
			middle = str1 + len;
			start = copy;
			end = str2 + len;
		}
	}

	/* if we found, replace and keep looking for the same string */
	if (start != NULL && middle != NULL && end != NULL) {
		temp = g_strdup_printf ("%s%s%s%s%s", start, left, middle, right, end);
		/* recursive */
		data = egg_markdown_to_text_line_formatter (temp, formatter, left, right);
		g_free (temp);
	} else {
		/* not found, keep return as-is */
		data = g_strdup (line);
	}
out:
	g_free (copy);
	return data;
}

/**
 * egg_markdown_to_text_line_pass2:
 *
 * All the formatting that requires paragraphs to be unwound first
 **/
static gchar *
egg_markdown_to_text_line_pass2 (const gchar *line, gboolean use_pango)
{
	gchar *data = g_strdup (line);
	gchar *temp;

	/* don't use pango markup unless widget supports that */
	if (use_pango) {
		/* bold */
		temp = data;
		data = egg_markdown_to_text_line_formatter (temp, "**", "<b>", "</b>");
		g_free (temp);

		/* italic */
		temp = data;
		data = egg_markdown_to_text_line_formatter (temp, "*", "<i>", "</i>");
		g_free (temp);

		/* fixed */
		temp = data;
		data = egg_markdown_to_text_line_formatter (temp, "`", "<tt>", "</tt>");
		g_free (temp);
	} else {
		/* bold */
		temp = data;
		data = egg_markdown_to_text_line_formatter (temp, "**", "", "");
		g_free (temp);

		/* italic */
		temp = data;
		data = egg_markdown_to_text_line_formatter (temp, "*", "", "");
		g_free (temp);

		/* fixed */
		temp = data;
		data = egg_markdown_to_text_line_formatter (temp, "`", "", "");
		g_free (temp);
	}

	/* double quotes */
	temp = data;
	data = egg_markdown_to_text_line_formatter (temp, "\"", "“", "”");
	g_free (temp);

	/* single quotes */
	temp = data;
	data = egg_markdown_to_text_line_formatter (temp, "'", "‘", "’");
	g_free (temp);

	return data;
}

/**
 * egg_markdown_to_text:
 **/
static gchar *
egg_markdown_to_text (const gchar *markdown, gint max_lines, gboolean use_pango)
{
	GString *string;
	gchar **lines;
	guint i;
	guint len;
	gchar *unwound;
	gchar *temp;

	/* do the first pass, unwinding paragraphs */
	string = g_string_new ("");
	lines = g_strsplit (markdown, "\n", -1);
	len = g_strv_length (lines);
	for (i=0; i<len; i++) {
		temp = egg_markdown_to_text_line_pass1 (lines[i]);
		if (temp != NULL)
			g_string_append (string, temp);
		g_free (temp);
	}
	g_strfreev (lines);

	/* remove trailing newlines */
	while (g_str_has_suffix (string->str, "\n"))
		g_string_set_size (string, string->len - 1);
	unwound = g_string_free (string, FALSE);

	/* do the second pass */
	string = g_string_new ("");
	lines = g_strsplit (unwound, "\n", -1);
	len = g_strv_length (lines);

	/* truncate number of lines */
	if (max_lines > 0 && len > max_lines)
		len = max_lines;
	for (i=0; i<len; i++) {
		/* strip trailing spaces */
		g_strchomp (lines[i]);
		temp = egg_markdown_to_text_line_pass2 (lines[i], use_pango);
		if (temp != NULL)
			g_string_append_printf (string, "%s\n", temp);
		g_free (temp);
	}
	g_strfreev (lines);

	/* remove trailing \n */
	while (g_str_has_suffix (string->str, "\n"))
		g_string_set_size (string, string->len - 1);

	/* free */
	g_free (unwound);
	return g_string_free (string, FALSE);
}

/**
 * egg_markdown_to_pango_markup:
 **/
gchar *
egg_markdown_to_pango_markup (const gchar *markdown, gint max_lines)
{
	return egg_markdown_to_text (markdown, max_lines, TRUE);
}

/**
 * egg_markdown_to_utf8:
 **/
gchar *
egg_markdown_to_utf8 (const gchar *markdown, gint max_lines)
{
	return egg_markdown_to_text (markdown, max_lines, FALSE);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_markdown_test (EggTest *test)
{
	gchar *text;
	gboolean ret;
	const gchar *markdown;
	const gchar *markdown_expected;

	if (!egg_test_start (test, "EggMarkdown"))
		return;

	/************************************************************
	 ****************        line_is_rule          **************
	 ************************************************************/
	ret = egg_markdown_to_text_line_is_rule ("* * *");
	egg_test_title_assert (test, "is rule (1)", ret);

	/************************************************************/
	ret = egg_markdown_to_text_line_is_rule ("***");
	egg_test_title_assert (test, "is rule (2)", ret);

	/************************************************************/
	ret = egg_markdown_to_text_line_is_rule ("*****");
	egg_test_title_assert (test, "is rule (3)", ret);

	/************************************************************/
	ret = egg_markdown_to_text_line_is_rule ("- - -");
	egg_test_title_assert (test, "is rule (4)", ret);

	/************************************************************/
	ret = egg_markdown_to_text_line_is_rule ("---------------------------------------");
	egg_test_title_assert (test, "is rule (5)", ret);

	/************************************************************/
	ret = egg_markdown_to_text_line_is_rule ("");
	egg_test_title_assert (test, "is rule (blank)", !ret);

	/************************************************************/
	ret = egg_markdown_to_text_line_is_rule ("richard hughes");
	egg_test_title_assert (test, "is rule (text)", !ret);

	/************************************************************/
	ret = egg_markdown_to_text_line_is_rule ("- richard-hughes");
	egg_test_title_assert (test, "is rule (bullet)", !ret);

	/************************************************************
	 ****************          formatter           **************
	 ************************************************************/
	text = egg_markdown_to_text_line_formatter ("**is important** text", "**", "<b>", "</b>");
	egg_test_title (test, "formatter (left)");
	if (egg_strequal (text, "<b>is important</b> text"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	text = egg_markdown_to_text_line_formatter ("this is **important**", "**", "<b>", "</b>");
	egg_test_title (test, "formatter (right)");
	if (egg_strequal (text, "this is <b>important</b>"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	text = egg_markdown_to_text_line_formatter ("**important**", "**", "<b>", "</b>");
	egg_test_title (test, "formatter (only)");
	if (egg_strequal (text, "<b>important</b>"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	text = egg_markdown_to_text_line_formatter ("this **is important** text in **several** places", "**", "<b>", "</b>");
	egg_test_title (test, "formatter (middle, multiple)");
	if (egg_strequal (text, "this <b>is important</b> text in <b>several</b> places"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	text = egg_markdown_to_text_line_formatter ("this was \"triffic\" it was", "\"", "“", "”");
	egg_test_title (test, "formatter (quotes)");
	if (egg_strequal (text, "this was “triffic” it was"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	text = egg_markdown_to_text_line_formatter ("This isn't a present", "'", "‘", "’");
	egg_test_title (test, "formatter (one quote)");
	if (egg_strequal (text, "This isn't a present"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************
	 ****************          markdown            **************
	 ************************************************************/
	markdown = "This is a spec file description or\n"
		   "an **update** description in bohdi.\n"
		   "\n"
		   "* * *\n"
		   "# Big title\n"
		   "\n"
		   "The *following* things 'were' fixed:\n"
		   "- Fix `dave`\n"
		   "* Fubar update because of \"security\"\n";
	markdown_expected =
		   "This is a spec file description or an <b>update</b> description in bohdi.\n"
		   "\n"
		   "⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯\n"
		   "<big>Big title</big>\n"
		   "\n"
		   "The <i>following</i> things ‘were’ fixed:\n"
		   "• Fix <tt>dave</tt>\n"
		   "• Fubar update because of “security”";
	egg_test_title (test, "markdown (complex)");
	text = egg_markdown_to_pango_markup (markdown, -1);
	if (egg_strequal (text, markdown_expected))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got '%s', expected '%s'", text, markdown_expected);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "markdown (one line limit)");
	text = egg_markdown_to_pango_markup (markdown, 1);
	if (egg_strequal (text, "This is a spec file description or an <b>update</b> description in bohdi."))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got '%s', expected '%s'", text, markdown_expected);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "markdown (free text)");
	text = egg_markdown_to_pango_markup ("This isn't a present", -1);
	if (egg_strequal (text, "This isn't a present"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got '%s'", text);
	g_free (text);

	egg_test_end (test);
}
#endif

