/*
 * Copyright (C) 2014  Red Hat, Inc.
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
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>
#include <unistd.h>
#include <glob.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/loop.h>
#include <errno.h>
#include <blockdev/utils.h>
#include "loop.h"

#ifndef LOOP_SET_BLOCK_SIZE
#define LOOP_SET_BLOCK_SIZE	0x4C09
#endif

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
 * bd_loop_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_loop_init (void) {
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
void bd_loop_close (void) {
    /* nothing to do here */
}


/**
 * bd_loop_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDLoopTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_loop_is_tech_avail (BDLoopTech tech G_GNUC_UNUSED, guint64 mode G_GNUC_UNUSED, GError **error G_GNUC_UNUSED) {
    /* all combinations are supported by this implementation of the plugin */
    return TRUE;
}

void bd_loop_info_free (BDLoopInfo *info) {
    if (info == NULL)
        return;

    g_free (info->backing_file);
    g_free (info);
}

BDLoopInfo* bd_loop_info_copy (BDLoopInfo *info) {
    if (info == NULL)
        return NULL;

    BDLoopInfo *new_info = g_new0 (BDLoopInfo, 1);

    new_info->backing_file = g_strdup (info->backing_file);
    new_info->offset = info->offset;
    new_info->autoclear = info->autoclear;
    new_info->direct_io = info->direct_io;
    new_info->part_scan = info->part_scan;
    new_info->read_only = info->read_only;

    return new_info;
}

static gchar* _loop_get_backing_file (const gchar *dev_name, GError **error) {
    gchar *sys_path = g_strdup_printf ("/sys/class/block/%s/loop/backing_file", dev_name);
    gchar *ret = NULL;
    gboolean success = FALSE;

    if (access (sys_path, R_OK) != 0) {
        g_free (sys_path);
        return NULL;
    }

    success = g_file_get_contents (sys_path, &ret, NULL, error);
    if (!success) {
        /* error is already populated */
        g_free (sys_path);
        return NULL;
    }

    g_free (sys_path);
    return g_strstrip (ret);
}

/**
 * bd_loop_info:
 * @loop: name of the loop device to get information about (e.g. "loop0")
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the @loop device or %NULL in case of error
 *
 * Tech category: %BD_LOOP_TECH_LOOP-%BD_LOOP_TECH_MODE_QUERY
 */
BDLoopInfo* bd_loop_info (const gchar *loop, GError **error) {
    BDLoopInfo *info = NULL;
    g_autofree gchar *dev_loop = NULL;
    gint fd = -1;
    struct loop_info64 li64;
    GError *l_error = NULL;

    if (!g_str_has_prefix (loop, "/dev/"))
        dev_loop = g_strdup_printf ("/dev/%s", loop);

    fd = open (dev_loop ? dev_loop : loop, O_RDONLY);
    if (fd < 0) {
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_DEVICE,
                     "Failed to open device %s: %m", loop);
        return NULL;
    }

    memset (&li64, 0, sizeof (li64));
    if (ioctl (fd, LOOP_GET_STATUS64, &li64) < 0) {
        g_set_error (error, BD_LOOP_ERROR,
                     errno == ENXIO ? BD_LOOP_ERROR_DEVICE : BD_LOOP_ERROR_FAIL,
                     "Failed to get status of the device %s: %m", loop);
        close (fd);
        return NULL;
    }

    close (fd);

    info = g_new0 (BDLoopInfo, 1);
    info->offset = li64.lo_offset;
    if ((li64.lo_flags & LO_FLAGS_AUTOCLEAR) != 0)
        info->autoclear = TRUE;
    if ((li64.lo_flags & LO_FLAGS_DIRECT_IO) != 0)
        info->direct_io = TRUE;
    if ((li64.lo_flags & LO_FLAGS_PARTSCAN) != 0)
        info->part_scan = TRUE;
    if ((li64.lo_flags & LO_FLAGS_READ_ONLY) != 0)
        info->read_only = TRUE;

    info->backing_file = _loop_get_backing_file (loop, &l_error);
    if (l_error) {
        bd_loop_info_free (info);
        g_set_error (error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to get backing file of the device %s: %s", loop, l_error->message);
        g_clear_error (&l_error);
        return NULL;
    }

    return info;
}

/**
 * bd_loop_get_loop_name:
 * @file: path of the backing file to get loop name for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): name of the loop device associated with the given
 * @file or %NULL if failed to determine
 *
 * Tech category: %BD_LOOP_TECH_LOOP-%BD_LOOP_TECH_MODE_QUERY
 */
gchar* bd_loop_get_loop_name (const gchar *file, GError **error G_GNUC_UNUSED) {
    glob_t globbuf;
    gchar **path_p;
    gboolean success = FALSE;
    gchar *content = NULL;
    gboolean found = FALSE;
    gchar **parts;
    gchar *ret;

    if (glob ("/sys/block/loop*/loop/backing_file", GLOB_NOSORT, NULL, &globbuf) != 0) {
        return NULL;
    }

    for (path_p = globbuf.gl_pathv; *path_p && !found; path_p++) {
        success = g_file_get_contents (*path_p, &content, NULL, NULL);
        if (!success)
            continue;

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
 * @sector_size: logical sector size for the loop device in bytes (or 0 for default)
 * @loop_name: (optional) (out): if not %NULL, it is used to store the name of the loop device
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @file was successfully setup as a loop device or not
 *
 * Tech category: %BD_LOOP_TECH_LOOP-%BD_LOOP_TECH_MODE_CREATE
 */
gboolean bd_loop_setup (const gchar *file, guint64 offset, guint64 size, gboolean read_only, gboolean part_scan, guint64 sector_size, const gchar **loop_name, GError **error) {
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

    ret = bd_loop_setup_from_fd (fd, offset, size, read_only, part_scan, sector_size, loop_name, error);
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
 * @sector_size: logical sector size for the loop device in bytes (or 0 for default)
 * @loop_name: (optional) (out): if not %NULL, it is used to store the name of the loop device
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an new loop device was successfully setup for @fd or not
 *
 * Tech category: %BD_LOOP_TECH_LOOP-%BD_LOOP_TECH_MODE_CREATE
 */
gboolean bd_loop_setup_from_fd (gint fd, guint64 offset, guint64 size, gboolean read_only, gboolean part_scan, guint64 sector_size, const gchar **loop_name, GError **error) {
    gint loop_control_fd = -1;
    gint loop_number = -1;
    gchar *loop_device = NULL;
    gint loop_fd = -1;
    struct loop_info64 li64;
    guint64 progress_id = 0;
    gint status = 0;
    guint n_try = 0;
    GError *l_error = NULL;

    progress_id = bd_utils_report_started ("Started setting up loop device");

    loop_control_fd = open ("/dev/loop-control", O_RDWR);
    if (loop_control_fd == -1) {
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to open the loop-control device: %m");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* XXX: serialize access to loop-control (seems to be required, but it's not
            documented anywhere) */
    g_mutex_lock (&loop_control_lock);
    loop_number = ioctl (loop_control_fd, LOOP_CTL_GET_FREE);
    g_mutex_unlock (&loop_control_lock);
    close (loop_control_fd);
    if (loop_number < 0) {
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to open the loop-control device: %m");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    bd_utils_report_progress (progress_id, 33, "Got free loop device");

    loop_device = g_strdup_printf ("/dev/loop%d", loop_number);
    loop_fd = open (loop_device, read_only ? O_RDONLY : O_RDWR);
    if (loop_fd == -1) {
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to open the %s device: %m", loop_device);
        g_free (loop_device);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
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
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_DEVICE,
                     "Failed to associate the %s device with the file descriptor: %m", loop_device);
        g_free (loop_device);
        close (loop_fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    bd_utils_report_progress (progress_id, 66, "Associated the loop device");

    /* we may need to try multiple times with some delays in case the device is
       busy at the very moment */
    for (n_try=10, status=-1; (status != 0) && (n_try > 0); n_try--) {
        status = ioctl (loop_fd, LOOP_SET_STATUS64, &li64);
        if (status < 0 && errno == EAGAIN)
            g_usleep (100 * 1000); /* microseconds */
        else
            break;
    }

    if (status != 0) {
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to set status for the %s device: %m", loop_device);
        g_free (loop_device);
        close (loop_fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (sector_size > 0) {
        for (n_try=10, status=-1; (status != 0) && (n_try > 0); n_try--) {
            status = ioctl (loop_fd, LOOP_SET_BLOCK_SIZE, (unsigned long) sector_size);
            if (status < 0 && errno == EAGAIN)
                g_usleep (100 * 1000); /* microseconds */
            else
                break;
        }

        if (status != 0) {
            g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                         "Failed to set sector size for the %s device: %m", loop_device);
            g_free (loop_device);
            close (loop_fd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
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
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @loop device was successfully torn down or not
 *
 * Tech category: %BD_LOOP_TECH_LOOP-%BD_LOOP_TECH_MODE_DESTROY
 */
gboolean bd_loop_teardown (const gchar *loop, GError **error) {
    gchar *dev_loop = NULL;
    gint loop_fd = -1;
    guint64 progress_id = 0;
    GError *l_error = NULL;

    progress_id = bd_utils_report_started ("Started tearing down loop device");

    if (!g_str_has_prefix (loop, "/dev/"))
        dev_loop = g_strdup_printf ("/dev/%s", loop);

    loop_fd = open (dev_loop ? dev_loop : loop, O_RDONLY);
    g_free (dev_loop);
    if (loop_fd == -1) {
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to open the %s device: %m", loop);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (ioctl (loop_fd, LOOP_CLR_FD) < 0) {
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to detach the backing file from the %s device: %m", loop);
        close (loop_fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    close (loop_fd);
    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
}

/**
 * bd_loop_set_autoclear:
 * @loop: path or name of the loop device
 * @autoclear: whether to set or unset the autoclear flag
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the autoclear flag was successfully set on the @loop device or not
 *
 * Tech category: %BD_LOOP_TECH_LOOP-%BD_LOOP_TECH_MODE_MODIFY
 */
gboolean bd_loop_set_autoclear (const gchar *loop, gboolean autoclear, GError **error) {
    gchar *dev_loop = NULL;
    gint fd = -1;
    struct loop_info64 li64;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    if (!g_str_has_prefix (loop, "/dev/"))
        dev_loop = g_strdup_printf ("/dev/%s", loop);

    msg = g_strdup_printf ("Started setting up the autoclear flag on the /dev/%s device",
                           dev_loop ? dev_loop : loop);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    fd = open (dev_loop ? dev_loop : loop, O_RDWR);
    g_free (dev_loop);
    if (fd < 0) {
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_DEVICE,
                     "Failed to open device %s: %m", loop);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    memset (&li64, 0, sizeof (li64));
    if (ioctl (fd, LOOP_GET_STATUS64, &li64) < 0) {
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to get status of the device %s: %m", loop);
        close (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (autoclear)
        li64.lo_flags |= LO_FLAGS_AUTOCLEAR;
    else
        li64.lo_flags &= (~LO_FLAGS_AUTOCLEAR);

    if (ioctl (fd, LOOP_SET_STATUS64, &li64) < 0) {
        g_set_error (&l_error, BD_LOOP_ERROR, BD_LOOP_ERROR_FAIL,
                     "Failed to set status of the device %s: %m", loop);
        close (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    close (fd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}
