/*
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vojtech Trefny <vtrefny@redhat.com>
 */

#include <glib.h>

#ifndef BD_VDO_STATS
#define BD_VDO_STATS

gboolean get_stat_val_double (GHashTable *stats, const gchar *key, gdouble *val);
gboolean get_stat_val64 (GHashTable *stats, const gchar *key, gint64 *val);
gboolean get_stat_val64_default (GHashTable *stats, const gchar *key, gint64 *val, gint64 def);

GHashTable* vdo_get_stats_full (const gchar *name, GError **error);

#endif  /* BD_VDO_STATS */
