/*
 * Copyright (C) 2014  Red Hat, Inc.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>
#include <glib/gprintf.h>
#include <sizes.h>
#include <math.h>
#include "lvm.h"

/**
 * SECTION: lvm
 * @short_description: libblockdev plugin for operations with LVM
 * @title: LVM
 * @include: lvm.h
 *
 * A libblockdev plugin for operations with LVM. All sizes passed in/out to/from
 * the functions are in bytes.
 */

static const gchar* supported_functions[] = {
    "bd_lvm_is_supported_pe_size",
    "bd_lvm_get_max_lv_size",
    "bd_lvm_round_size_to_pe",
    "bd_lvm_get_lv_physical_size",
    "bd_lvm_get_thpool_padding",
    NULL};

const gchar** get_supported_functions () {
    return supported_functions;
}

static gboolean call_lvm (gchar **args, gchar **stdout_data, gchar **stderr_data,
                          gint *status, gchar **error_message)
{
    guint i = 0;
    gboolean success;
    GError *error = NULL;

    guint args_length = g_strv_length (args);
    gchar **argv = g_new (gchar*, args_length + 1);

    /* construct argv from args with "lvm" prepended */
    argv[0] = "lvm";
    for (i=0; i < args_length; i++)
        argv[i+1] = args[i];
    argv[args_length + 1] = NULL;

    success = g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT|G_SPAWN_SEARCH_PATH,
                            NULL, NULL, stdout_data, stderr_data, status, &error);

    if (!success) {
        *error_message = g_strdup (error->message);
        g_error_free (error);
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_lvm_is_supported_pe_size:
 *
 * @size: size (in bytes) to test
 *
 * Return: whether the given size is supported physical extent size or not
 */
gboolean bd_lvm_is_supported_pe_size (guint64 size) {
    return (((size % 2) == 0) && (size >= (1 KiB)) && (size <= (16 GiB)));
}

/**
 * bd_lvm_get_max_lv_size:
 * Return: maximum LV size in bytes
 */
guint64 bd_lvm_get_max_lv_size () {
    return MAX_LV_SIZE;
}

/**
 * bd_lvm_round_size_to_pe:
 * @size: size to be rounded
 * @pe_size: physical extent (PE) size or 0 to use the default
 * @roundup: whether to round up or down (ceil or floor)
 *
 * Return: @size rounded to @pe_size according to the @roundup
 *
 * Rounds given @size up/down to a multiple of @pe_size according to the value of
 * the @roundup parameter.
 */
guint64 bd_lvm_round_size_to_pe (guint64 size, guint64 pe_size, gboolean roundup) {
    pe_size = RESOLVE_PE_SIZE(pe_size);
    guint64 delta = size % pe_size;
    if (delta == 0)
        return size;

    if (roundup)
        return size + (pe_size - delta);
    else
        return size - delta;
}

/**
 * bd_lvm_get_lv_physical_size:
 * @lv_size: LV size
 * @pe_size: PE size
 *
 * Return: space taken on disk(s) by the LV with given @size
 *
 * Gives number of bytes needed for an LV with the size @lv_size on an LVM stack
 * using given @pe_size.
 */
guint64 bd_lvm_get_lv_physical_size (guint64 lv_size, guint64 pe_size) {
    /* TODO: should take into account mirroring and RAID in general? */

    /* add one PE for metadata */
    pe_size = RESOLVE_PE_SIZE(pe_size);

    return bd_lvm_round_size_to_pe (lv_size, pe_size, TRUE) + pe_size;
}

/**
 * bd_lvm_get_thpool_padding:
 * @size: size of the thin pool
 * @pe_size: PE size or 0 if the default value should be used
 * @included: if padding is already included in the size
 *
 * Return: size of the padding needed for a thin pool with the given @size
 *         according to the @pe_size and @included
 */
guint64 bd_lvm_get_thpool_padding (guint64 size, guint64 pe_size, gboolean included) {
    guint64 raw_md_size;
    pe_size = RESOLVE_PE_SIZE(pe_size);

    if (included)
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_EXISTS);
    else
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_NEW);

    return MIN (bd_lvm_round_size_to_pe(raw_md_size, pe_size, TRUE),
                bd_lvm_round_size_to_pe(MAX_THPOOL_MD_SIZE, pe_size, TRUE));
}

int main (int argc, char **argv) {
    gint exit_status;
    const gchar **fname = NULL;
    gchar *msg = NULL;
    gchar *stdout = NULL;
    gchar *stderr = NULL;
    gboolean succ;
    guint64 result = 0;

    g_printf ("Supported functions:\n");
    for (fname=get_supported_functions(); (*fname); fname++) {
        puts ((*fname));
    }
    puts ("");

    gchar* args[] = {"lvs", "--all", NULL};

    /* test calling lvm */
    g_printf ("Calling\n");
    succ = call_lvm (args, &stdout, &stderr, &exit_status, &msg);
    g_printf ("Called\n");
    if (succ) {
        puts ("Everything ok\n");
        g_printf ("STDOUT: %s\n", stdout);
        g_printf ("STDERR: %s\n", stderr);
        g_printf ("Exit status: %d\n", exit_status);
    }
    else {
        puts ("Some error!");
        puts (msg);
    }

    if (stdout)
        g_free (stdout);
    if (stderr)
        g_free (stderr);
    if (msg)
        g_free (msg);

    if (bd_lvm_is_supported_pe_size(16 MEBIBYTE))
        puts ("16 MiB PE: Supported.");
    else
        puts ("16 MiB PE: Unsupported.");

    g_printf ("max LV size: %s\n", bd_size_human_readable(bd_lvm_get_max_lv_size()));

    result = bd_lvm_round_size_to_pe ((13 MiB), USE_DEFAULT_PE_SIZE, TRUE);
    g_printf ("up-rounded size 13 MiB: %s\n", bd_size_human_readable(result));
    result = bd_lvm_round_size_to_pe ((13 MiB), USE_DEFAULT_PE_SIZE, FALSE);
    g_printf ("down-rounded size 13 MiB: %s\n", bd_size_human_readable(result));

    result = bd_lvm_get_lv_physical_size ((13 MiB), USE_DEFAULT_PE_SIZE);
    g_printf ("13 MiB physical size: %s\n", bd_size_human_readable(result));

    result = bd_lvm_get_thpool_padding ((1 GiB), USE_DEFAULT_PE_SIZE, TRUE);
    g_printf ("1 GiB ThPool padding size (included): %s\n", bd_size_human_readable(result));
    result = bd_lvm_get_thpool_padding ((1 GiB), USE_DEFAULT_PE_SIZE, FALSE);
    g_printf ("1 GiB ThPool padding size (not included): %s\n", bd_size_human_readable(result));

    return 0;
}
