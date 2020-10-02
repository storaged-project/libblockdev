/*
 * Copyright (C) 2020  Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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
 * Author: Vojtech Trefny <vtrefny@redhat.com>
 *
 * This is just a small helper program for (V)FAT filesystem resizing using libparted.
 * We don't want this to be part of the library because libparted is licensed under GPLv3
 * and we want to keep libblockdev LGPL compatible.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <parted/parted.h>
#include <bytesize/bs_size.h>


void print_usage (const char *cmd) {
    g_print ("Usage: %s device [size]\n"
             "-h    --help   Print this usage info\n"
             "If size is not specified the file system is adapted to the underlying block device\n",
             cmd);
}

PedExceptionOption bd_exc_handler (PedException *ex) {
    if (ex->type <= PED_EXCEPTION_WARNING && (ex->options & PED_EXCEPTION_IGNORE) != 0) {
      g_print ("[parted] %s\n", ex->message);
      return PED_EXCEPTION_IGNORE;
    }
    g_printerr ("[parted] %s\n",ex->message);
    return PED_EXCEPTION_UNHANDLED;
}

int main (int argc, char *argv[]) {
    PedDevice *ped_dev = NULL;
    PedGeometry geom = {0};
    PedGeometry new_geom = {0};
    PedFileSystem *fs = NULL;
    PedSector start = 0;
    PedSector length = 0;
    gint status = 0;
    guint64 new_size = 0;
    BSSize bs_size;
    BSError *bs_error = NULL;
    gchar *device = NULL;

    if (argc < 2 || g_strcmp0 (argv[1], "-h") == 0 || g_strcmp0 (argv[1], "--help") == 0) {
        print_usage (argv[0]);
        return 0;
    }

    if (argc == 3) {
        device = argv[1];
        bs_size = bs_size_new_from_str (argv[2], &bs_error);
        if (bs_size) {
            new_size = bs_size_get_bytes (bs_size, NULL, &bs_error);
            bs_size_free (bs_size);
        }
        if (bs_error) {
            g_printerr ("Failed to parse size from '%s': '%s'\n", argv[2], bs_error->msg);
            bs_clear_error (&bs_error);
            return 1;
        }
    } else
        device = argv[1];

    ped_exception_set_handler ((PedExceptionHandler*) bd_exc_handler);

    ped_dev = ped_device_get (device);
    if (!ped_dev) {
        g_printerr ("Failed to get ped device for the device '%s'\n", device);
        return 1;
    }

    status = ped_device_open (ped_dev);
    if (status == 0) {
        g_printerr ("Failed to get open the device '%s'\n", device);
        return 1;
    }

    status = ped_geometry_init (&geom, ped_dev, start, ped_dev->length);
    if (status == 0) {
        g_printerr ("Failed to initialize geometry for the device '%s'\n", device);
        ped_device_close (ped_dev);
        return 1;
    }

    fs = ped_file_system_open (&geom);
    if (!fs) {
        g_printerr ("Failed to read the filesystem on the device '%s'\n", device);
        ped_device_close (ped_dev);
        return 1;
    }

    if (new_size == 0)
        length = ped_dev->length;
    else
        length = (PedSector) ((PedSector) new_size / ped_dev->sector_size);

    status = ped_geometry_init (&new_geom, ped_dev, start, length);
    if (status == 0) {
        g_printerr ("Failed to initialize new geometry for the filesystem on '%s'\n", device);
        ped_file_system_close (fs);
        ped_device_close (ped_dev);
        return 1;
    }

    status = ped_file_system_resize (fs, &new_geom, NULL);
    if (status == 0) {
        g_printerr ("Failed to resize the filesystem on '%s'\n", device);
        ped_file_system_close (fs);
        ped_device_close (ped_dev);
        return 1;
    }

    ped_file_system_close (fs);
    ped_device_close (ped_dev);

    return 0;
}
