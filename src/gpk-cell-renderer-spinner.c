/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Matthias Clasen <mclasen@redhat.com>
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

#include <gpk-cell-renderer-spinner.h>

enum {
	PROP_0,
	PROP_PULSE,
	PROP_SIZE
};

struct _GpkCellRendererSpinnerPrivate
{
	gint pulse;
	GtkIconSize size;
	gint n_images;
	GdkPixbuf **images;
};

#define GPK_CELL_RENDERER_SPINNER_GET_PRIVATE(object)	\
		(G_TYPE_INSTANCE_GET_PRIVATE ((object),	\
			GPK_TYPE_CELL_RENDERER_SPINNER, \
			GpkCellRendererSpinnerPrivate))

static void gpk_cell_renderer_spinner_finalize		(GObject		*object);
static void gpk_cell_renderer_spinner_get_property	(GObject		*object,
							 guint			 param_id,
							 GValue			*value,
							 GParamSpec		*pspec);
static void gpk_cell_renderer_spinner_set_property	(GObject		*object,
							 guint			 param_id,
							 const GValue		*value,
							 GParamSpec		*pspec);
static void gpk_cell_renderer_spinner_get_size		(GtkCellRenderer	*cell,
							 GtkWidget		*widget,
							 GdkRectangle		*cell_area,
							 gint			*x_offset,
							 gint			*y_offset,
							 gint			*width,
							 gint			*height);
static void gpk_cell_renderer_spinner_render		(GtkCellRenderer	*cell,
							 GdkWindow		*window,
							 GtkWidget		*widget,
							 GdkRectangle		*background_area,
							 GdkRectangle		*cell_area,
							 GdkRectangle		*expose_area,
							 guint			 flags);


G_DEFINE_TYPE (GpkCellRendererSpinner, gpk_cell_renderer_spinner, GTK_TYPE_CELL_RENDERER)

static void
gpk_cell_renderer_spinner_class_init (GpkCellRendererSpinnerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS (klass);

	object_class->finalize = gpk_cell_renderer_spinner_finalize;
	object_class->get_property = gpk_cell_renderer_spinner_get_property;
	object_class->set_property = gpk_cell_renderer_spinner_set_property;

	cell_class->get_size = gpk_cell_renderer_spinner_get_size;
	cell_class->render = gpk_cell_renderer_spinner_render;

	g_object_class_install_property (object_class,
					 PROP_PULSE,
					 g_param_spec_int ("pulse",
						 "Pulse",
						 "Pulse of the spinner",
						 -1, G_MAXINT, -1,
						 G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_SIZE,
					 g_param_spec_uint ("size",
						 "Size",
						 "The GtkIconSize value that specifies the size of the rendered spinner",
						 0, G_MAXUINT,
						 GTK_ICON_SIZE_MENU,
						 G_PARAM_READWRITE));


	g_type_class_add_private (object_class, sizeof (GpkCellRendererSpinnerPrivate));
}

static void
gpk_cell_renderer_spinner_init (GpkCellRendererSpinner *cell)
{
	GpkCellRendererSpinnerPrivate *priv = GPK_CELL_RENDERER_SPINNER_GET_PRIVATE (cell);

	priv->pulse = -1;
	priv->size = GTK_ICON_SIZE_MENU;
	priv->n_images = 0;
	priv->images = NULL;
	cell->priv = priv;
}

GtkCellRenderer*
gpk_cell_renderer_spinner_new (void)
{
	return g_object_new (GPK_TYPE_CELL_RENDERER_SPINNER, NULL);
}

static void
gpk_cell_renderer_spinner_finalize (GObject *object)
{
	GpkCellRendererSpinner *cell = GPK_CELL_RENDERER_SPINNER (object);
	GpkCellRendererSpinnerPrivate *priv = cell->priv;
	gint i;

	for (i = 0; i < priv->n_images; i++)
		g_object_unref (priv->images[i]);
	g_free (priv->images);
	priv->images = NULL;
	priv->n_images = 0;

	G_OBJECT_CLASS (gpk_cell_renderer_spinner_parent_class)->finalize (object);
}

static void
gpk_cell_renderer_spinner_ensure_images (GpkCellRendererSpinner *cell,
GtkWidget *widget)
{
	GpkCellRendererSpinnerPrivate *priv = cell->priv;
	GdkScreen *screen;
	GtkIconTheme *icon_theme;
	GtkSettings *settings;
	gint width, height;
	gint i, j;
	GdkPixbuf *pixbuf;
	gint tile_x = 8; /* FIXME: should determine from the image */
	gint tile_y = 4;

	if (priv->images)
		return;

	priv->n_images = tile_x * tile_y;
	priv->images = g_new (GdkPixbuf*, priv->n_images);

	screen = gtk_widget_get_screen (GTK_WIDGET (widget));
	icon_theme = gtk_icon_theme_get_for_screen (screen);
	settings = gtk_settings_get_for_screen (screen);

	if (!gtk_icon_size_lookup_for_settings (settings, priv->size, &width, &height)) {
		g_warning ("Invalid icon size %u\n", priv->size);
		width = height = 24;
	}

	pixbuf = gtk_icon_theme_load_icon (icon_theme,
					   "process-working",
					   MIN (width, height),
					   GTK_ICON_LOOKUP_USE_BUILTIN,
					   NULL);

	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);

	egg_debug ("pixbuf: %dx%d", width, height);

	width = width / tile_x;
	height = height / tile_y;

	egg_debug ("tile: %dx%d\n", width, height);

	for (i = 0; i < tile_y; i++) {
		for (j = 0; j < tile_x; j++) {
			priv->images[i*tile_x + j] = gdk_pixbuf_new_subpixbuf (pixbuf, j * width, i * height, width, height);
		}
	}

	g_object_unref (pixbuf);
}

static void
gpk_cell_renderer_spinner_get_property (GObject *object, guint param_id, GValue *value, GParamSpec *pspec)
{
	GpkCellRendererSpinner *cell = GPK_CELL_RENDERER_SPINNER (object);
	GpkCellRendererSpinnerPrivate *priv = cell->priv;

	switch (param_id) {
	case PROP_PULSE:
		g_value_set_int (value, priv->pulse);
		break;
	case PROP_SIZE:
		g_value_set_uint (value, priv->size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
	}
}

static void
gpk_cell_renderer_spinner_set_property (GObject *object, guint param_id, const GValue *value, GParamSpec *pspec)
{
	GpkCellRendererSpinner *cell = GPK_CELL_RENDERER_SPINNER (object);
	GpkCellRendererSpinnerPrivate *priv = cell->priv;

	switch (param_id) {
	case PROP_PULSE:
		priv->pulse = g_value_get_int (value);
		break;
	case PROP_SIZE:
		priv->size = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
	}
}

static void
gpk_cell_renderer_spinner_get_size (GtkCellRenderer *cellr, GtkWidget *widget, GdkRectangle *cell_area,
				    gint *x_offset, gint *y_offset, gint *width, gint *height)
{
	GpkCellRendererSpinner *cell = GPK_CELL_RENDERER_SPINNER (cellr);
	GpkCellRendererSpinnerPrivate *priv = cell->priv;
	gdouble align;
	gint w, h;
	gboolean rtl;

	rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;

	gpk_cell_renderer_spinner_ensure_images (cell, widget);

	w = gdk_pixbuf_get_width (priv->images[0]) + 2 * cellr->xpad;
	h = gdk_pixbuf_get_height (priv->images[0]) + 2 * cellr->ypad;

	if (cell_area) {
		if (x_offset) {
			align = rtl ? 1.0 - cellr->xalign : cellr->xalign;
			*x_offset = align * (cell_area->width - w);
			*x_offset = MAX (*x_offset, 0);
		}
		if (y_offset) {
			align = rtl ? 1.0 - cellr->yalign : cellr->yalign;
			*y_offset = align * (cell_area->height - h);
			*y_offset = MAX (*y_offset, 0);
		}
	} else {
		if (x_offset)
			*x_offset = 0;
		if (y_offset)
			*y_offset = 0;
	}

	if (width)
		*width = w;
	if (height)
		*height = h;
}

static void
gpk_cell_renderer_spinner_render (GtkCellRenderer *cellr, GdkWindow *window, GtkWidget *widget, GdkRectangle *background_area,
				  GdkRectangle *cell_area, GdkRectangle *expose_area, guint flags)
{
	GpkCellRendererSpinner *cell = GPK_CELL_RENDERER_SPINNER (cellr);
	GpkCellRendererSpinnerPrivate *priv = cell->priv;
	GdkPixbuf *pixbuf;
	GdkRectangle pix_rect;
	GdkRectangle draw_rect;
	cairo_t *cr;

	if (priv->pulse < 0)
		return;

	gpk_cell_renderer_spinner_get_size (cellr, widget, cell_area,
					    &pix_rect.x, &pix_rect.y,
					    &pix_rect.width, &pix_rect.height);

	pix_rect.x += cell_area->x + cellr->xpad;
	pix_rect.y += cell_area->y + cellr->ypad;
	pix_rect.width -= cellr->xpad * 2;
	pix_rect.height -= cellr->ypad * 2;

	if (!gdk_rectangle_intersect (cell_area, &pix_rect, &draw_rect) ||
			!gdk_rectangle_intersect (expose_area, &draw_rect, &draw_rect))
		return;

	if (priv->pulse == 0)
		pixbuf = priv->images[0]; /* FIXME: this interpretation of tile 0 is not specified anywhere */
	else
		pixbuf = priv->images[1 + priv->pulse % (priv->n_images - 1)];
	g_object_ref (pixbuf);

	if (GTK_WIDGET_STATE (widget) == GTK_STATE_INSENSITIVE || !cellr->sensitive) {
		GtkIconSource *source;

		source = gtk_icon_source_new ();
		gtk_icon_source_set_pixbuf (source, pixbuf);
		/* The size here is arbitrary; since size isn't
		 * wildcarded in the source, it isn't supposed to be
		 * scaled by the engine function
		 */
		gtk_icon_source_set_size (source, GTK_ICON_SIZE_MENU);
		gtk_icon_source_set_size_wildcarded (source, FALSE);

		g_object_unref (pixbuf);
		pixbuf = gtk_style_render_icon (widget->style, source,
						gtk_widget_get_direction (widget),
						GTK_STATE_INSENSITIVE,
						/* arbitrary */
						(GtkIconSize)-1,
						widget,
						"gtkcellrendererpixbuf");
		 gtk_icon_source_free (source);
	}

	cr = gdk_cairo_create (window);

	gdk_cairo_set_source_pixbuf (cr, pixbuf, pix_rect.x, pix_rect.y);
	gdk_cairo_rectangle (cr, &draw_rect);
	cairo_fill (cr);

	cairo_destroy (cr);

	g_object_unref (pixbuf);
}

