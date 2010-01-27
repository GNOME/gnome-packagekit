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

#include "egg-debug.h"

#include "gpk-language.h"

static void     gpk_language_finalize	(GObject	  *object);

#define GPK_LANGUAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_LANGUAGE, GpkLanguagePrivate))

struct GpkLanguagePrivate
{
	GHashTable		*hash;
};

G_DEFINE_TYPE (GpkLanguage, gpk_language, G_TYPE_OBJECT)

/**
 * gpk_language_parser_start_element:
 **/
static void
gpk_language_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
				   const gchar **attribute_names, const gchar **attribute_values,
				   gpointer user_data, GError **error)
{
	guint i, len;
	const gchar *code1 = NULL;
	const gchar *code2b = NULL;
	const gchar *name = NULL;
	GpkLanguage *language = user_data;

	if (strcmp (element_name, "iso_639_entry") != 0)
		return;

	/* find data */
	len = g_strv_length ((gchar**)attribute_names);
	for (i=0; i<len; i++) {
		if (strcmp (attribute_names[i], "iso_639_1_code") == 0)
			code1 = attribute_values[i];
		if (strcmp (attribute_names[i], "iso_639_2B_code") == 0)
			code2b = attribute_values[i];
		if (strcmp (attribute_names[i], "name") == 0)
			name = attribute_values[i];
	}

	/* not valid entry */
	if (name == NULL)
		return;

	/* add both to hash */
	if (code1 != NULL)
		g_hash_table_insert (language->priv->hash, g_strdup (code1), g_strdup (name));
	if (code2b != NULL)
		g_hash_table_insert (language->priv->hash, g_strdup (code2b), g_strdup (name));
}

/* trivial parser */
static const GMarkupParser gpk_language_markup_parser =
{
	gpk_language_parser_start_element,
	NULL, /* end_element */
	NULL, /* characters */
	NULL, /* passthrough */
	NULL /* error */
};

/**
 * gpk_language_populate:
 *
 * <iso_639_entry iso_639_2B_code="hun" iso_639_2T_code="hun" iso_639_1_code="hu" name="Hungarian" />
 **/
gboolean
gpk_language_populate (GpkLanguage *language, GError **error)
{
	gboolean ret = FALSE;
	gchar *contents = NULL;
	gchar *filename;
	gsize size;
	GMarkupParseContext *context = NULL;

	/* find filename */
	filename = g_build_filename (DATADIR, "xml", "iso-codes", "iso_639.xml", NULL);
	if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
		g_free (filename);
		filename = g_build_filename ("/usr", "share", "xml", "iso-codes", "iso_639.xml", NULL);
	}
	if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
		g_set_error (error, 1, 0, "cannot find source file : '%s'", filename);
		goto out;
	}

	/* get contents */
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_language_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, language, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (filename);
	g_free (contents);
	return ret;
}

/**
 * gpk_language_iso639_to_language:
 **/
gchar *
gpk_language_iso639_to_language (GpkLanguage *language, const gchar *iso639)
{
	return g_strdup (g_hash_table_lookup (language->priv->hash, iso639));
}

/**
 * gpk_language_finalize:
 * @object: The object to finalize
 **/
static void
gpk_language_finalize (GObject *object)
{
	GpkLanguage *language;

	g_return_if_fail (GPK_IS_LANGUAGE (object));

	language = GPK_LANGUAGE (object);

	g_return_if_fail (language->priv != NULL);
	g_hash_table_unref (language->priv->hash);

	G_OBJECT_CLASS (gpk_language_parent_class)->finalize (object);
}

/**
 * gpk_language_init:
 * @language: This class instance
 **/
static void
gpk_language_init (GpkLanguage *language)
{
	language->priv = GPK_LANGUAGE_GET_PRIVATE (language);
	language->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, (GDestroyNotify) g_free);
}

/**
 * gpk_language_class_init:
 * @klass: The GpkLanguageClass
 **/
static void
gpk_language_class_init (GpkLanguageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_language_finalize;
	g_type_class_add_private (klass, sizeof (GpkLanguagePrivate));
}

/**
 * gpk_language_new:
 *
 * Return value: a new GpkLanguage object.
 **/
GpkLanguage *
gpk_language_new (void)
{
	GpkLanguage *language;
	language = g_object_new (GPK_TYPE_LANGUAGE, NULL);
	return GPK_LANGUAGE (language);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_language_test (EggTest *test)
{
	gboolean ret;
	gchar *lang;
	GError *error = NULL;
	GpkLanguage *language = NULL;

	if (!egg_test_start (test, "GpkLanguage"))
		return;

	/************************************************************/
	egg_test_title (test, "get GpkLanguage object");
	language = gpk_language_new ();
	if (language != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "populate");
	ret = gpk_language_populate (language, &error);
	if (ret) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed to load XML: %s", error->message);
		g_error_free (error);
	}

	/************************************************************/
	egg_test_title (test, "get data (present)");
	lang = gpk_language_iso639_to_language (language, "en");
	if (lang != NULL && strcmp (lang, "English") == 0)
		egg_test_success (test, "got %s", lang);
	else
		egg_test_failed (test, NULL);
	g_free (lang);

	/************************************************************/
	egg_test_title (test, "get data (missing)");
	lang = gpk_language_iso639_to_language (language, "notgoingtoexist");
	if (lang == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %s", lang);
	g_free (lang);

	g_object_unref (language);

	egg_test_end (test);
}
#endif

