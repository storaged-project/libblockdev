/*
 * Copyright (C) 2014-2024 Red Hat, Inc.
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
 * Author: Tomas Bzatek <tbzatek@redhat.com>
 */

#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "smart.h"
#include "smart-private.h"


void free_drivedb_attrs (DriveDBAttr **attrs) {
    DriveDBAttr **a;

    if (attrs == NULL)
        return;
    for (a = attrs; *a; a++) {
        g_free ((*a)->name);
        g_free (*a);
    }
    g_free (attrs);
}


#ifndef HAVE_DRIVEDB_H
DriveDBAttr** drivedb_lookup_drive (G_GNUC_UNUSED const gchar *model, G_GNUC_UNUSED const gchar *fw, G_GNUC_UNUSED gboolean include_defaults) {
    return NULL;
}
#else

struct drive_settings {
    const char* modelfamily;
    const char* modelregexp;
    const char* firmwareregexp;
    const char* warningmsg;
    const char* presets;
};

static const struct drive_settings builtin_knowndrives[] = {
#include <drivedb.h>
};


static gboolean parse_attribute_def (const char *arg, gint *attr_id, gchar **attr_name) {
    int format_str_len = 0;
    int attrname_str_len = 0;
    int hddssd_str_len = 0;
    char format_str[33] = {0,};
    char attrname_str[33] = {0,};
    char hddssd_str[4] = {0,};

    if (arg[0] == 'N')
        /* ignoring "N,format[,name]" as it doesn't provide attribute ID */
        return FALSE;

    /* parse "id,format[+][,name[,HDD|SSD]]" */
    if (sscanf (arg, "%d,%32[^,]%n,%32[^,]%n,%3[DHS]%n",
                attr_id,
                format_str, &format_str_len,
                attrname_str, &attrname_str_len,
                hddssd_str, &hddssd_str_len) < 2)
        return FALSE;
    if (*attr_id < 1 || *attr_id > 255)
        return FALSE;
    if (attrname_str_len < 1)
        return FALSE;

    /* ignoring format_str */
    *attr_name = g_strndup (attrname_str, attrname_str_len);
    /* ignoring hddssd_str */

    return TRUE;
}

static void parse_presets_str (const char *presets, GHashTable *attrs) {
    while (TRUE) {
        char opt;
        char arg[94] = {0,};
        int len = 0;
        gint attr_id = 0;
        gchar *attr_name = NULL;

        presets += strspn (presets, " \t");
        if (presets[0] == '\0')
            break;
        if (sscanf (presets, "-%c %80[^ ]%n", &opt, arg, &len) < 2)
            break;
        if (len < 1)
            break;
        if (opt == 'v') {
            /* parse "-v N,format[,name[,HDD|SSD]]" */
            if (parse_attribute_def (arg, &attr_id, &attr_name)) {
                g_hash_table_replace (attrs, GINT_TO_POINTER (attr_id), attr_name);
            }
        }
        /* ignoring other switches like 'F' and 'd' */

        presets += len;
    }
}

DriveDBAttr** drivedb_lookup_drive (const gchar *model, const gchar *fw, gboolean include_defaults) {
    gulong i;
    GHashTable *attrs;
    DriveDBAttr **ret = NULL;

    if (G_N_ELEMENTS (builtin_knowndrives) == 0)
        return NULL;

    attrs = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

    /* first parse the DEFAULTS definitions */
    if (include_defaults)
        for (i = 0; i < G_N_ELEMENTS (builtin_knowndrives); i++)
            if (builtin_knowndrives[i].modelfamily &&
                builtin_knowndrives[i].presets &&
                g_ascii_strncasecmp (builtin_knowndrives[i].modelfamily, "DEFAULT", 7) == 0)
                    parse_presets_str (builtin_knowndrives[i].presets, attrs);

    /* now overlay/replace with drive specific keys */
    for (i = 0; i < G_N_ELEMENTS (builtin_knowndrives); i++) {
        GRegex *regex;
        GError *error = NULL;
        gboolean match = FALSE;

        /* check the modelfamily string */
        if (builtin_knowndrives[i].modelfamily == NULL ||
            builtin_knowndrives[i].modelregexp == NULL ||
            builtin_knowndrives[i].presets == NULL || strlen (builtin_knowndrives[i].presets) < 5 ||
            g_ascii_strncasecmp (builtin_knowndrives[i].modelfamily, "VERSION", 7) == 0 ||
            g_ascii_strncasecmp (builtin_knowndrives[i].modelfamily, "USB", 3) == 0 ||
            g_ascii_strncasecmp (builtin_knowndrives[i].modelfamily, "DEFAULT", 7) == 0)
            continue;
        /* assuming modelfamily=ATA from now on... */

        /* match the model string */
        regex = g_regex_new (builtin_knowndrives[i].modelregexp,
                             0, 0, &error);
        if (regex == NULL) {
            bd_utils_log_format (BD_UTILS_LOG_DEBUG,
                                 "drivedb-parser: regex compilation failed for '%s': %s",
                                 builtin_knowndrives[i].modelregexp,
                                 error->message);
            g_error_free (error);
            continue;
        }
        if (g_regex_match (regex, model, 0, NULL))
            match = TRUE;
        g_regex_unref (regex);
        if (!match)
            continue;

        /* match the firmware string */
        if (builtin_knowndrives[i].firmwareregexp &&
            strlen (builtin_knowndrives[i].firmwareregexp) > 0 &&
            fw && strlen (fw) > 0)
        {
            regex = g_regex_new (builtin_knowndrives[i].firmwareregexp,
                                 0, 0, &error);
            if (regex == NULL) {
                bd_utils_log_format (BD_UTILS_LOG_DEBUG,
                                     "drivedb-parser: regex compilation failed for '%s': %s",
                                     builtin_knowndrives[i].firmwareregexp,
                                     error->message);
                g_error_free (error);
                continue;
            }
            if (!g_regex_match (regex, model, 0, NULL))
                match = FALSE;
            g_regex_unref (regex);
        }

        if (match)
            parse_presets_str (builtin_knowndrives[i].presets, attrs);
    }

    if (g_hash_table_size (attrs) > 0) {
        GHashTableIter iter;
        gpointer key, val;

        /* convert to NULL-terminated array */
        i = 0;
        ret = g_new0 (DriveDBAttr *, g_hash_table_size (attrs) + 1);
        g_hash_table_iter_init (&iter, attrs);
        while (g_hash_table_iter_next (&iter, &key, &val)) {
            DriveDBAttr *attr;

            attr = g_new0 (DriveDBAttr, 1);
            attr->id = GPOINTER_TO_INT (key);
            attr->name = g_strdup (val);
            ret[i++] = attr;
        }
    }
    g_hash_table_destroy (attrs);

    return ret;
}
#endif   /* HAVE_DRIVEDB_H */
