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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <glib.h>

#include "egg-string.h"
#include "egg-markdown.h"

/*******************************************************************************
 *
 * This is a simple Markdown parser.
 * It can output to Pango, HTML or plain text. The following limitations are
 * already known, and properly deliberate:
 *
 * - No code section support
 * - No ordered list support
 * - No blockquote section support
 * - No image support
 * - No links or email support
 * - No backslash escapes support
 * - No HTML escaping support
 * - Auto-escapes certain word patterns, like http://
 *
 * It does support the rest of the standard pretty well, although it's not
 * been run against any conformance tests. The parsing is single pass, with
 * a simple enumerated intepretor mode and a single line back-memory.
 *
 ******************************************************************************/

static void     egg_markdown_finalize	(GObject		*object);

#define EGG_MARKDOWN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), EGG_TYPE_MARKDOWN, EggMarkdownPrivate))

#define EGG_MARKDOWN_MAX_LINE_LENGTH	1024

typedef enum {
	EGG_MARKDOWN_MODE_BLANK,
	EGG_MARKDOWN_MODE_RULE,
	EGG_MARKDOWN_MODE_BULLETT,
	EGG_MARKDOWN_MODE_PARA,
	EGG_MARKDOWN_MODE_H1,
	EGG_MARKDOWN_MODE_H2,
	EGG_MARKDOWN_MODE_UNKNOWN
} EggMarkdownMode;

typedef struct {
	const gchar *em_start;
	const gchar *em_end;
	const gchar *strong_start;
	const gchar *strong_end;
	const gchar *code_start;
	const gchar *code_end;
	const gchar *h1_start;
	const gchar *h1_end;
	const gchar *h2_start;
	const gchar *h2_end;
	const gchar *bullett_start;
	const gchar *bullett_end;
	const gchar *rule;
} EggMarkdownTags;

struct EggMarkdownPrivate
{
	EggMarkdownMode		 mode;
	EggMarkdownTags		 tags;
	EggMarkdownOutput	 output;
	gint			 max_lines;
	guint			 line_count;
	gboolean		 smart_quoting;
	gboolean		 escape;
	gboolean		 autocode;
	GString			*pending;
	GString			*processed;
};

G_DEFINE_TYPE (EggMarkdown, egg_markdown, G_TYPE_OBJECT)

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
 * egg_markdown_to_text_line_is_bullett:
 **/
static gboolean
egg_markdown_to_text_line_is_bullett (const gchar *line)
{
	return (g_str_has_prefix (line, "- ") ||
		g_str_has_prefix (line, "* ") ||
		g_str_has_prefix (line, "+ ") ||
		g_str_has_prefix (line, " - ") ||
		g_str_has_prefix (line, " * ") ||
		g_str_has_prefix (line, " + "));
}

/**
 * egg_markdown_to_text_line_is_header1:
 **/
static gboolean
egg_markdown_to_text_line_is_header1 (const gchar *line)
{
	return g_str_has_prefix (line, "# ");
}

/**
 * egg_markdown_to_text_line_is_header2:
 **/
static gboolean
egg_markdown_to_text_line_is_header2 (const gchar *line)
{
	return g_str_has_prefix (line, "## ");
}

/**
 * egg_markdown_to_text_line_is_header1_type2:
 **/
static gboolean
egg_markdown_to_text_line_is_header1_type2 (const gchar *line)
{
	return g_str_has_prefix (line, "===");
}

/**
 * egg_markdown_to_text_line_is_header2_type2:
 **/
static gboolean
egg_markdown_to_text_line_is_header2_type2 (const gchar *line)
{
	return g_str_has_prefix (line, "---");
}

#if 0
/**
 * egg_markdown_to_text_line_is_code:
 **/
static gboolean
egg_markdown_to_text_line_is_code (const gchar *line)
{
	return (g_str_has_prefix (line, "    ") || g_str_has_prefix (line, "\t"));
}

/**
 * egg_markdown_to_text_line_is_blockquote:
 **/
static gboolean
egg_markdown_to_text_line_is_blockquote (const gchar *line)
{
	return (g_str_has_prefix (line, "> "));
}
#endif

/**
 * egg_markdown_to_text_line_is_blank:
 **/
static gboolean
egg_markdown_to_text_line_is_blank (const gchar *line)
{
	guint i;
	guint len;
	gboolean ret = FALSE;

	len = egg_strlen (line, EGG_MARKDOWN_MAX_LINE_LENGTH);

	/* a line with no characters is blank by definition */
	if (len == 0) {
		ret = TRUE;
		goto out;
	}

	/* find if there are only space chars */
	for (i=0; i<len; i++) {
		if (line[i] != ' ' && line[i] != '\t')
			goto out;
	}

	/* if we matched, return true */
	ret = TRUE;
out:
	return ret;
}

/**
 * egg_markdown_replace:
 **/
static gchar *
egg_markdown_replace (const gchar *haystack, const gchar *needle, const gchar *replace)
{
	gchar *new;
	gchar **split;

	split = g_strsplit (haystack, needle, -1);
	new = g_strjoinv (replace, split);
	g_strfreev (split);

	return new;
}

/**
 * egg_markdown_strstr_spaces:
 **/
static gchar *
egg_markdown_strstr_spaces (const gchar *haystack, const gchar *needle)
{
	gchar *found;
	const gchar *haystack_new = haystack;

retry:
	/* don't find if surrounded by spaces */
	found = strstr (haystack_new, needle);
	if (found == NULL)
		return NULL;

	/* start of the string, always valid */
	if (found == haystack)
		return found;

	/* end of the string, always valid */
	if (*(found-1) == ' ' && *(found+1) == ' ') {
		haystack_new = found+1;
		goto retry;
	}
	return found;
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
	str1 = egg_markdown_strstr_spaces (copy, formatter);
	if (str1 != NULL) {
		*str1 = '\0';
		str2 = egg_markdown_strstr_spaces (str1+len, formatter);
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
 * egg_markdown_to_text_line_format_sections:
 **/
static gchar *
egg_markdown_to_text_line_format_sections (EggMarkdown *self, const gchar *line)
{
	gchar *data = g_strdup (line);
	gchar *temp;

	/* bold1 */
	temp = data;
	data = egg_markdown_to_text_line_formatter (temp, "**", self->priv->tags.strong_start, self->priv->tags.strong_end);
	g_free (temp);

	/* bold2 */
	temp = data;
	data = egg_markdown_to_text_line_formatter (temp, "__", self->priv->tags.strong_start, self->priv->tags.strong_end);
	g_free (temp);

	/* italic1 */
	temp = data;
	data = egg_markdown_to_text_line_formatter (temp, "*", self->priv->tags.em_start, self->priv->tags.em_end);
	g_free (temp);

	/* italic2 */
	temp = data;
	data = egg_markdown_to_text_line_formatter (temp, "_", self->priv->tags.em_start, self->priv->tags.em_end);
	g_free (temp);

	/* em-dash */
	temp = data;
	data = egg_markdown_replace (temp, " -- ", " — ");
	g_free (temp);

	/* smart quoting */
	if (self->priv->smart_quoting) {
		temp = data;
		data = egg_markdown_to_text_line_formatter (temp, "\"", "“", "”");
		g_free (temp);

		temp = data;
		data = egg_markdown_to_text_line_formatter (temp, "'", "‘", "’");
		g_free (temp);
	}

	return data;
}

/**
 * egg_markdown_to_text_line_format:
 **/
static gchar *
egg_markdown_to_text_line_format (EggMarkdown *self, const gchar *line)
{
	guint i;
	gchar *text;
	gboolean mode = FALSE;
	gchar **codes;
	GString *string;

	/* optimise the trivial case where we don't have any code tags */
	text = strstr (line, "`");
	if (text == NULL) {
		text = egg_markdown_to_text_line_format_sections (self, line);
		goto out;
	}

	/* we want to parse the code sections without formatting */
	codes = g_strsplit (line, "`", -1);
	string = g_string_new ("");
	for (i=0; codes[i] != NULL; i++) {
		if (!mode) {
			text = egg_markdown_to_text_line_format_sections (self, codes[i]);
			g_string_append (string, text);
			g_free (text);
			mode = TRUE;
		} else {
			/* just append without formatting */
			g_string_append (string, self->priv->tags.code_start);
			g_string_append (string, codes[i]);
			g_string_append (string, self->priv->tags.code_end);
			mode = FALSE;
		}
	}
	text = g_string_free (string, FALSE);
out:
	return text;
}

/**
 * egg_markdown_add_pending:
 **/
static gboolean
egg_markdown_add_pending (EggMarkdown *self, const gchar *line)
{
	gchar *copy;

	/* would put us over the limit */
	if (self->priv->line_count >= self->priv->max_lines)
		return FALSE;

	copy = g_strdup (line);

	/* strip leading and trailing spaces */
	g_strstrip (copy);

	/* append */
	g_string_append_printf (self->priv->pending, "%s ", copy);

	g_free (copy);
	return TRUE;
}

/**
 * egg_markdown_add_pending_header:
 **/
static gboolean
egg_markdown_add_pending_header (EggMarkdown *self, const gchar *line)
{
	gchar *copy;
	gboolean ret;

	/* strip trailing # */
	copy = g_strdup (line);
	g_strdelimit (copy, "#", ' ');
	ret = egg_markdown_add_pending (self, copy);
	g_free (copy);
	return ret;
}

/**
 * egg_markdown_count_chars_in_word:
 **/
static guint
egg_markdown_count_chars_in_word (const gchar *text, gchar find)
{
	guint i;
	guint len;
	guint count = 0;

	/* get length */
	len = egg_strlen (text, EGG_MARKDOWN_MAX_LINE_LENGTH);
	if (len == 0)
		goto out;

	/* find matching chars */
	for (i=0; i<len; i++) {
		if (text[i] == find)
			count++;
	}
out:
	return count;
}

/**
 * egg_markdown_word_is_code:
 **/
static gboolean
egg_markdown_word_is_code (const gchar *text)
{
	/* already code */
	if (g_str_has_prefix (text, "`"))
		return FALSE;
	if (g_str_has_suffix (text, "`"))
		return FALSE;

	/* paths */
	if (g_str_has_prefix (text, "/"))
		return TRUE;

	/* bugzillas */
	if (g_str_has_prefix (text, "#"))
		return TRUE;

	/* uri's */
	if (g_str_has_prefix (text, "http://"))
		return TRUE;
	if (g_str_has_prefix (text, "https://"))
		return TRUE;
	if (g_str_has_prefix (text, "ftp://"))
		return TRUE;

	/* patch files */
	if (g_strrstr (text, ".patch") != NULL)
		return TRUE;
	if (g_strrstr (text, ".diff") != NULL)
		return TRUE;

	/* function names */
	if (g_strrstr (text, "()") != NULL)
		return TRUE;

	/* email addresses */
	if (g_strrstr (text, "@") != NULL)
		return TRUE;

	/* compiler defines */
	if (text[0] != '_' &&
	    egg_markdown_count_chars_in_word (text, '_') > 1)
		return TRUE;

	/* nothing special */
	return FALSE;
}

/**
 * egg_markdown_word_auto_format_code:
 **/
static gchar *
egg_markdown_word_auto_format_code (const gchar *text)
{
	guint i;
	gchar *temp;
	gchar **words;
	gboolean ret = FALSE;

	/* split sentence up with space */
	words = g_strsplit (text, " ", -1);

	/* search each word */
	for (i=0; words[i] != NULL; i++) {
		if (egg_markdown_word_is_code (words[i])) {
			temp = g_strdup_printf ("`%s`", words[i]);
			g_free (words[i]);
			words[i] = temp;
			ret = TRUE;
		}
	}

	/* no replacements, so just return a copy */
	if (!ret) {
		temp = g_strdup (text);
		goto out;
	}

	/* join the array back into a string */
	temp = g_strjoinv (" ", words);
out:
	g_strfreev (words);
	return temp;
}

/**
 * egg_markdown_flush_pending:
 **/
static void
egg_markdown_flush_pending (EggMarkdown *self)
{
	gchar *copy;
	gchar *temp;

	/* no data yet */
	if (self->priv->mode == EGG_MARKDOWN_MODE_UNKNOWN)
		return;

	/* remove trailing spaces */
	while (g_str_has_suffix (self->priv->pending->str, " "))
		g_string_set_size (self->priv->pending, self->priv->pending->len - 1);

	/* pango requires escaping */
	copy = g_strdup (self->priv->pending->str);
	if (!self->priv->escape && self->priv->output == EGG_MARKDOWN_OUTPUT_PANGO) {
		g_strdelimit (copy, "<", '(');
		g_strdelimit (copy, ">", ')');
	}

	/* check words for code */
	if (self->priv->autocode &&
	    (self->priv->mode == EGG_MARKDOWN_MODE_PARA ||
	     self->priv->mode == EGG_MARKDOWN_MODE_BULLETT)) {
		temp = egg_markdown_word_auto_format_code (copy);
		g_free (copy);
		copy = temp;
	}

	/* escape */
	if (self->priv->escape) {
		temp = g_markup_escape_text (copy, -1);
		g_free (copy);
		copy = temp;
	}

	/* do formatting */
	temp = egg_markdown_to_text_line_format (self, copy);
	if (self->priv->mode == EGG_MARKDOWN_MODE_BULLETT) {
		g_string_append_printf (self->priv->processed, "%s%s%s\n", self->priv->tags.bullett_start, temp, self->priv->tags.bullett_end);
		self->priv->line_count++;
	} else if (self->priv->mode == EGG_MARKDOWN_MODE_H1) {
		g_string_append_printf (self->priv->processed, "%s%s%s\n", self->priv->tags.h1_start, temp, self->priv->tags.h1_end);
	} else if (self->priv->mode == EGG_MARKDOWN_MODE_H2) {
		g_string_append_printf (self->priv->processed, "%s%s%s\n", self->priv->tags.h2_start, temp, self->priv->tags.h2_end);
	} else if (self->priv->mode == EGG_MARKDOWN_MODE_PARA ||
		   self->priv->mode == EGG_MARKDOWN_MODE_RULE) {
		g_string_append_printf (self->priv->processed, "%s\n", temp);
		self->priv->line_count++;
	}

	g_debug ("adding '%s'", temp);

	/* clear */
	g_string_truncate (self->priv->pending, 0);
	g_free (copy);
	g_free (temp);
}

/**
 * egg_markdown_to_text_line_process:
 **/
static gboolean
egg_markdown_to_text_line_process (EggMarkdown *self, const gchar *line)
{
	gboolean ret;

	/* blank */
	ret = egg_markdown_to_text_line_is_blank (line);
	if (ret) {
		g_debug ("blank: '%s'", line);
		egg_markdown_flush_pending (self);
		/* a new line after a list is the end of list, not a gap */
		if (self->priv->mode != EGG_MARKDOWN_MODE_BULLETT)
			ret = egg_markdown_add_pending (self, "\n");
		self->priv->mode = EGG_MARKDOWN_MODE_BLANK;
		goto out;
	}

	/* header1_type2 */
	ret = egg_markdown_to_text_line_is_header1_type2 (line);
	if (ret) {
		g_debug ("header1_type2: '%s'", line);
		if (self->priv->mode == EGG_MARKDOWN_MODE_PARA)
			self->priv->mode = EGG_MARKDOWN_MODE_H1;
		goto out;
	}

	/* header2_type2 */
	ret = egg_markdown_to_text_line_is_header2_type2 (line);
	if (ret) {
		g_debug ("header2_type2: '%s'", line);
		if (self->priv->mode == EGG_MARKDOWN_MODE_PARA)
			self->priv->mode = EGG_MARKDOWN_MODE_H2;
		goto out;
	}

	/* rule */
	ret = egg_markdown_to_text_line_is_rule (line);
	if (ret) {
		g_debug ("rule: '%s'", line);
		egg_markdown_flush_pending (self);
		self->priv->mode = EGG_MARKDOWN_MODE_RULE;
		ret = egg_markdown_add_pending (self, self->priv->tags.rule);
		goto out;
	}

	/* bullett */
	ret = egg_markdown_to_text_line_is_bullett (line);
	if (ret) {
		g_debug ("bullett: '%s'", line);
		egg_markdown_flush_pending (self);
		self->priv->mode = EGG_MARKDOWN_MODE_BULLETT;
		ret = egg_markdown_add_pending (self, &line[2]);
		goto out;
	}

	/* header1 */
	ret = egg_markdown_to_text_line_is_header1 (line);
	if (ret) {
		g_debug ("header1: '%s'", line);
		egg_markdown_flush_pending (self);
		self->priv->mode = EGG_MARKDOWN_MODE_H1;
		ret = egg_markdown_add_pending_header (self, &line[2]);
		goto out;
	}

	/* header2 */
	ret = egg_markdown_to_text_line_is_header2 (line);
	if (ret) {
		g_debug ("header2: '%s'", line);
		egg_markdown_flush_pending (self);
		self->priv->mode = EGG_MARKDOWN_MODE_H2;
		ret = egg_markdown_add_pending_header (self, &line[3]);
		goto out;
	}

	/* paragraph */
	if (self->priv->mode == EGG_MARKDOWN_MODE_BLANK || self->priv->mode == EGG_MARKDOWN_MODE_UNKNOWN) {
		egg_markdown_flush_pending (self);
		self->priv->mode = EGG_MARKDOWN_MODE_PARA;
	}

	/* add to pending */
	g_debug ("continue: '%s'", line);
	ret = egg_markdown_add_pending (self, line);
out:
	/* if we failed to add, we don't know the mode */
	if (!ret)
		self->priv->mode = EGG_MARKDOWN_MODE_UNKNOWN;
	return ret;
}

/**
 * egg_markdown_set_output:
 **/
gboolean
egg_markdown_set_output (EggMarkdown *self, EggMarkdownOutput output)
{
	gboolean ret = TRUE;
	g_return_val_if_fail (EGG_IS_MARKDOWN (self), FALSE);

	/* PangoMarkup */
	if (output == EGG_MARKDOWN_OUTPUT_PANGO) {
		self->priv->tags.em_start = "<i>";
		self->priv->tags.em_end = "</i>";
		self->priv->tags.strong_start = "<b>";
		self->priv->tags.strong_end = "</b>";
		self->priv->tags.code_start = "<tt>";
		self->priv->tags.code_end = "</tt>";
		self->priv->tags.h1_start = "<big>";
		self->priv->tags.h1_end = "</big>";
		self->priv->tags.h2_start = "<b>";
		self->priv->tags.h2_end = "</b>";
		self->priv->tags.bullett_start = "• ";
		self->priv->tags.bullett_end = "";
		self->priv->tags.rule = "⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯\n";

	/* XHTML */
	} else if (output == EGG_MARKDOWN_OUTPUT_HTML) {
		self->priv->tags.em_start = "<em>";
		self->priv->tags.em_end = "<em>";
		self->priv->tags.strong_start = "<strong>";
		self->priv->tags.strong_end = "</strong>";
		self->priv->tags.code_start = "<code>";
		self->priv->tags.code_end = "</code>";
		self->priv->tags.h1_start = "<h1>";
		self->priv->tags.h1_end = "</h1>";
		self->priv->tags.h2_start = "<h2>";
		self->priv->tags.h2_end = "</h2>";
		self->priv->tags.bullett_start = "<li>";
		self->priv->tags.bullett_end = "</li>";
		self->priv->tags.rule = "<hr>";

	/* plain text */
	} else if (output == EGG_MARKDOWN_OUTPUT_TEXT) {
		self->priv->tags.em_start = "";
		self->priv->tags.em_end = "";
		self->priv->tags.strong_start = "";
		self->priv->tags.strong_end = "";
		self->priv->tags.code_start = "";
		self->priv->tags.code_end = "";
		self->priv->tags.h1_start = "[";
		self->priv->tags.h1_end = "]";
		self->priv->tags.h2_start = "-";
		self->priv->tags.h2_end = "-";
		self->priv->tags.bullett_start = "* ";
		self->priv->tags.bullett_end = "";
		self->priv->tags.rule = " ----- \n";

	/* unknown */
	} else {
		g_warning ("unknown output enum");
		ret = FALSE;
	}

	/* save if valid */
	if (ret)
		self->priv->output = output;
	return ret;
}

/**
 * egg_markdown_set_max_lines:
 **/
gboolean
egg_markdown_set_max_lines (EggMarkdown *self, gint max_lines)
{
	g_return_val_if_fail (EGG_IS_MARKDOWN (self), FALSE);
	self->priv->max_lines = max_lines;
	return TRUE;
}

/**
 * egg_markdown_set_smart_quoting:
 **/
gboolean
egg_markdown_set_smart_quoting (EggMarkdown *self, gboolean smart_quoting)
{
	g_return_val_if_fail (EGG_IS_MARKDOWN (self), FALSE);
	self->priv->smart_quoting = smart_quoting;
	return TRUE;
}

/**
 * egg_markdown_set_escape:
 **/
gboolean
egg_markdown_set_escape (EggMarkdown *self, gboolean escape)
{
	g_return_val_if_fail (EGG_IS_MARKDOWN (self), FALSE);
	self->priv->escape = escape;
	return TRUE;
}

/**
 * egg_markdown_set_autocode:
 **/
gboolean
egg_markdown_set_autocode (EggMarkdown *self, gboolean autocode)
{
	g_return_val_if_fail (EGG_IS_MARKDOWN (self), FALSE);
	self->priv->autocode = autocode;
	return TRUE;
}

/**
 * egg_markdown_parse:
 **/
gchar *
egg_markdown_parse (EggMarkdown *self, const gchar *markdown)
{
	gchar **lines;
	guint i;
	guint len;
	gchar *temp;
	gboolean ret;

	g_return_val_if_fail (EGG_IS_MARKDOWN (self), NULL);
	g_return_val_if_fail (self->priv->output != EGG_MARKDOWN_OUTPUT_UNKNOWN, NULL);

	g_debug ("input='%s'", markdown);

	/* process */
	self->priv->mode = EGG_MARKDOWN_MODE_UNKNOWN;
	self->priv->line_count = 0;
	g_string_truncate (self->priv->pending, 0);
	g_string_truncate (self->priv->processed, 0);
	lines = g_strsplit (markdown, "\n", -1);
	len = g_strv_length (lines);

	/* process each line */
	for (i=0; i<len; i++) {
		ret = egg_markdown_to_text_line_process (self, lines[i]);
		if (!ret)
			break;
	}
	g_strfreev (lines);
	egg_markdown_flush_pending (self);

	/* remove trailing \n */
	while (g_str_has_suffix (self->priv->processed->str, "\n"))
		g_string_set_size (self->priv->processed, self->priv->processed->len - 1);

	/* get a copy */
	temp = g_strdup (self->priv->processed->str);
	g_string_truncate (self->priv->pending, 0);
	g_string_truncate (self->priv->processed, 0);

	g_debug ("output='%s'", temp);

	return temp;
}

/**
 * egg_markdown_class_init:
 * @klass: The EggMarkdownClass
 **/
static void
egg_markdown_class_init (EggMarkdownClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = egg_markdown_finalize;
	g_type_class_add_private (klass, sizeof (EggMarkdownPrivate));
}

/**
 * egg_markdown_init:
 **/
static void
egg_markdown_init (EggMarkdown *self)
{
	self->priv = EGG_MARKDOWN_GET_PRIVATE (self);

	self->priv->mode = EGG_MARKDOWN_MODE_UNKNOWN;
	self->priv->output = EGG_MARKDOWN_OUTPUT_UNKNOWN;
	self->priv->pending = g_string_new ("");
	self->priv->processed = g_string_new ("");
	self->priv->max_lines = -1;
	self->priv->smart_quoting = FALSE;
	self->priv->escape = FALSE;
	self->priv->autocode = FALSE;
}

/**
 * egg_markdown_finalize:
 * @object: The object to finalize
 **/
static void
egg_markdown_finalize (GObject *object)
{
	EggMarkdown *self;

	g_return_if_fail (EGG_IS_MARKDOWN (object));

	self = EGG_MARKDOWN (object);

	g_return_if_fail (self->priv != NULL);
	g_string_free (self->priv->pending, TRUE);
	g_string_free (self->priv->processed, TRUE);

	G_OBJECT_CLASS (egg_markdown_parent_class)->finalize (object);
}

/**
 * egg_markdown_new:
 *
 * Return value: a new EggMarkdown object.
 **/
EggMarkdown *
egg_markdown_new (void)
{
	EggMarkdown *self;
	self = g_object_new (EGG_TYPE_MARKDOWN, NULL);
	return EGG_MARKDOWN (self);
}
