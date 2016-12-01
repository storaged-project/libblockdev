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
#include <unistd.h>
#include <glob.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/loop.h>
#include <blockdev/utils.h>
#include "loop.h"

/**
 * SECTION: loop
 * @short_description: plugin for operations with loop devices
 * @title: Loop
 * @include: loop.h
 *
 * A plugin for operations with loop devices. All sizes passed
 * in/out to/from the functions are in bytes.
 */

static GMutex loop_control_lock;

/**
 * bd_loop_error_quark: (skip)
 */
GQuark bd_loop_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-loop-error-quark");
}

/**
 * bd_loop_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_loop_check_deps () {
    GError *error = NULL;
    gboolean ret = bd_utils_check_util_version ("losetup", LOSETUP_MIN_VERSION, NULL, "losetup from util-linux\\s+([\\d\\.]+)", &error);

    if (!ret && error) {
        g_warning("Cannot load the loop plugin: %s" , error->message);
        g_clear_error (&error);
    }
    return ret;
}

/**
 * bd_loop_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_loop_init () {
    /* nothing to do here */
    return TRUE;
};

/**
 * bd_loop_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_loop_close () {
    /* nothing to do here */
}

/**
 * bd_loop_get_backing_file:
 * @dev_name: name of the loop device to get backing file for (e.g. "loop0")
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): path of the device's backing file or %NULL if none
 *                           is found
 */
gchar* bd_loop_get_backing_file (const gchar *dev_name, GError **error) {
    gchar *sys_path = g_strdup_printf ("/sys/class/block/%s/loop/backing_file", dev_name);
    gchar *ret = NULL;
    gboolean success = FALSE;

    if (access (sys_path, R_OK) != 0) {
        g_free (sys_path);
        return NULL;
    }

    success = g_file_get_contents (sys_path, &ret, NULL, error);
    if (!success) {
        /* error is alraedy populated */
        g_free (sys_path);
        return NULL;
    }

    g_free (sys_path);
    return g_strstrip (ret);
}

/**
 * bd_loop_get_loop_name:
 * @file: path of the backing file to get loop name for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): name of the loop device associated with the given
 * @file or %NULL if failed to determine
 */
gchar* bd_loop_get_loop_name (const gchar *file, GError **error __attribute__((unused))) {
    glob_t globbuf;
    gchar **path_p;
    gboolean success = FALSE;
    GError *tmp_error = NULL;
    gchar *content;
    gboolean found = FALSE;
    gchar **parts;
    gchar *ret;

    if (glob ("/sys/block/loop*/loop/backing_file", GLOB_NOSORT, NULL, &globbuf) != 0) {
        return NULL;
    }

    for (path_p = globbuf.gl_pathv; *path_p && !found; path_p++) {
        success = g_file_get_contents (*path_p, &content, NULL, &tmp_error);
        if (!success) {
            g_clear_error (&tmp_error);
            continue;
        }

        g_strstrip (content);
        found = (g_strcmp0 (content, file) == 0);
        g_free (content);
    }

    if (!found) {
        globfree (&globbuf);
        return NULL;
    }

    parts = g_strsplit (*(path_p - 1), "/", 5);
    ret = g_strdup (parts[3]);
    g_strfreev (parts);

    globfree (&globbuf);
    return ret;
}

/**
 * bd_loop_setup:
 * @file: file to setup as a loop device
 * @offset: offset of the start of the device (in @file)
 * @size: maximum size of the device (or 0 to leave unspecified)
 * @read_only: whether to setup as read-only (%TRUE) or read-write (%FALSE)
 * @part_scan: whether to enforce partition scan on the newly created device or not
 * @loop_name: (allow-none) (out): if not %NULL, it is used to store the name of the loop device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @file was successfully setup as a loop device or not
 */
gboolean bd_loop_setup (const gchar *file, guint64 offset, guint64 size, gboolean read_only, gboolean part_scan, const gchar **loop_name, GError **error) {
    gint fd = -1;
    gboolean ret = FALSE;

    /* open as RDWR so that @read_only determines whether the device is
       read-only or not */
    fd = open (file, O_RDWR);
    if (fd < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to open the backing file '%s': %m", file);
        return FALSE;
    }

    ret = bd_loop_setup_from_fd (fd, offset, size, read_only, part_scan, loop_name, error);
    close (fd);
    return ret;
}

/**
 * bd_loop_setup_from_fd:
 * @fd: file descriptor for a file to setup as a new loop device
 * @offset: offset of the start of the device (in file given by @fd)
 * @size: maximum size of the device (or 0 to leave unspecified)
 * @read_only: whether to setup as read-only (%TRUE) or read-write (%FALSE)
 * @part_scan: whether to enforce partition scan on the newly created device or not
 * @loop_name: (allow-none) (out): if not %NULL, it is used to store the name of the loop device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an new loop device was successfully setup for @fd or not
 */
gboolean bd_loop_setup_from_fd (gint fd, guint64 offset, guint64 size, gboolean read_only, gboolean part_scan, const gchar **loop_name, GError **error) {
    gint loop_control_fd = -1;
    gint loop_number = -1;
    gchar *loop_device = NULL;
    gint loop_fd = -1;
    struct loop_info64 li64;
    guint64 progress_id = 0;

    progress_id = bd_utils_report_started ("Started setting up loop device");

    loop_control_fd = open ("/dev/loop-control", O_RDWR);
    if (loop_control_fd == -1) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to open the loop-control device: %m");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* XXX: serialize access to loop-control (seems to be required, but it's not
            documented anywhere) */
    g_mutex_lock (&loop_control_lock);
    loop_number = ioctl (loop_control_fd, LOOP_CTL_GET_FREE);
    g_mutex_unlock (&loop_control_lock);
    close (loop_control_fd);
    if (loop_number < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to open the loop-control device: %m");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_progress (progress_id, 33, "Got free loop device");

    loop_device = g_strdup_printf ("/dev/loop%d", loop_number);
    loop_fd = open (loop_device, read_only ? O_RDONLY : O_RDWR);
    if (loop_fd == -1) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to open the %s device: %m", loop_device);
        g_free (loop_device);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    memset (&li64, '\0', sizeof (li64));
    if (read_only)
        li64.lo_flags |= LO_FLAGS_READ_ONLY;
    if (part_scan)
        li64.lo_flags |= LO_FLAGS_PARTSCAN;
    if (offset > 0)
        li64.lo_offset = offset;
    if (size > 0)
        li64.lo_sizelimit = size;
    if (ioctl (loop_fd, LOOP_SET_FD, fd) < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_DEVICE,
                     "Failed to associate the %s device with the file descriptor: %m", loop_device);
        g_free (loop_device);
        close (loop_fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_progress (progress_id, 66, "Associated the loop device");

    if (ioctl (loop_fd, LOOP_SET_STATUS64, &li64) < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to set status for the %s device: %m", loop_device);
        g_free (loop_device);
        close (loop_fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (loop_name)
        *loop_name = g_strdup_printf ("loop%d", loop_number);

    g_free (loop_device);
    close (loop_fd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_loop_teardown:
 * @loop: path or name of the loop device to tear down
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @loop device was successfully torn down or not
 */
gboolean bd_loop_teardown (const gchar *loop, GError **error) {
    gboolean success = FALSE;
    gchar *dev_loop = NULL;

    const gchar *args[4] = {"losetup", "-d", NULL, NULL};

    if (g_str_has_prefix (loop, "/dev/"))
        args[2] = loop;
    else {
        dev_loop = g_strdup_printf ("/dev/%s", loop);
        args[2] = dev_loop;
    }

    success = bd_utils_exec_and_report_error (args, NULL, error);
    g_free (dev_loop);

    return success;
}

/**
 * bd_loop_get_autoclear:
 * @loop: path or name of the loop device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the autoclear flag is set on the @loop device or not (if %FALSE, @error may be set)
 */
gboolean bd_loop_get_autoclear (const gchar *loop, GError **error) {
    gchar *dev_loop = NULL;
    gint fd = -1;
    struct loop_info64 li64;

    if (!g_str_has_prefix (loop, "/dev/"))
        dev_loop = g_strdup_printf ("/dev/%s", loop);

    fd = open (dev_loop ? dev_loop : loop, O_RDWR);
    g_free (dev_loop);
    if (fd < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_DEVICE,
                     "Failed to open device %s: %m", loop);
        return FALSE;
    }

    memset (&li64, 0, sizeof (li64));
    if (ioctl (fd, LOOP_GET_STATUS64, &li64) < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to get status of the device %s: %m", loop);
        close (fd);
        return FALSE;
    }

    close (fd);
    return (li64.lo_flags & LO_FLAGS_AUTOCLEAR) != 0;
}

/**
 * bd_loop_set_autoclear:
 * @loop: path or name of the loop device
 * @autoclear: whether to set or unset the autoclear flag
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the autoclear flag was successfully set on the @loop device or not
 */
gboolean bd_loop_set_autoclear (const gchar *loop, gboolean autoclear, GError **error) {
    gchar *dev_loop = NULL;
    gint fd = -1;
    struct loop_info64 li64;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    if (!g_str_has_prefix (loop, "/dev/"))
        dev_loop = g_strdup_printf ("/dev/%s", loop);

    msg = g_strdup_printf ("Started setting up the autoclear flag on the /dev/%s device",
                           dev_loop ? dev_loop : loop);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    fd = open (dev_loop ? dev_loop : loop, O_RDWR);
    g_free (dev_loop);
    if (fd < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_DEVICE,
                     "Failed to open device %s: %m", loop);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    memset (&li64, 0, sizeof (li64));
    if (ioctl (fd, LOOP_GET_STATUS64, &li64) < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to get status of the device %s: %m", loop);
        close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (autoclear)
        li64.lo_flags |= LO_FLAGS_AUTOCLEAR;
    else
        li64.lo_flags &= (~LO_FLAGS_AUTOCLEAR);

    if (ioctl (fd, LOOP_SET_STATUS64, &li64) < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to set status of the device %s: %m", loop);
        close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}
