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

#ifndef __GPK_STATE_H
#define __GPK_STATE_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	GPK_STATE_INSTALLED,
	GPK_STATE_INSTALLED_TO_BE_REMOVED,
	GPK_STATE_AVAILABLE,
	GPK_STATE_AVAILABLE_TO_BE_INSTALLED,
	GPK_STATE_UNKNOWN
} GpkPackageState;

gboolean	 gpk_application_state_invert		(GpkPackageState	*state);
gboolean	 gpk_application_state_select		(GpkPackageState	*state);
gboolean	 gpk_application_state_unselect		(GpkPackageState	*state);
const gchar	*gpk_application_state_get_icon		(GpkPackageState	 state);
gboolean	 gpk_application_state_get_checkbox	(GpkPackageState	 state);
gboolean	 gpk_application_state_installed	(GpkPackageState	 state);
gboolean	 gpk_application_state_in_queue		(GpkPackageState	 state);

G_END_DECLS

#endif	/* __GPK_STATE_H */
