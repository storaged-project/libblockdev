/*
 * Copyright (C) 2015  Red Hat, Inc.
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
 * Author: Samantha N. Bueno <sbueno@redhat.com>
 */

#include <glib.h>
#include <glob.h>
#include <linux/fs.h>
#include <stdio.h>
#include <string.h>
#include <blockdev/utils.h>
#include <asm/dasd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "s390.h"
#include "check_deps.h"

/**
 * SECTION: s390
 * @short_description: plugin for operations with s390
 * @title: s390
 * @include: s390.h
 *
 * A plugin for operations with s390 devices.
 */

/**
 * bd_s390_error_quark: (skip)
 */
GQuark bd_s390_error_quark (void) {
    return g_quark_from_static_string ("g-bd-s390-error-quark");
}


static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_DASDFMT 0
#define DEPS_DASDFMT_MASK (1 << DEPS_DASDFMT)
#define DEPS_ZKEY 1
#define DEPS_ZKEY_MASK (1 << DEPS_ZKEY)
#define DEPS_LAST 2

static const UtilDep deps[DEPS_LAST] = {
    /* dasdfmt doesn't return version info */
    {"dasdfmt", NULL, NULL, NULL},
    {"zkey", NULL, NULL, NULL},
};


/**
 * bd_s390_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_s390_init (void) {
    /* nothing to do here */
    return TRUE;
};

/**
 * bd_s390_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_s390_close (void) {
    g_atomic_int_set (&avail_deps, 0);
}

/**
 * bd_s390_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDS390TechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_s390_is_tech_avail (BDS390Tech tech, guint64 mode, GError **error) {
    switch (tech) {
    case BD_S390_TECH_ZFCP:
        /* all ZFCP-mode combinations are supported by this implementation of the
         * plugin, nothing extra is needed */
        return TRUE;
    case BD_S390_TECH_DASD:
        if (mode & BD_S390_TECH_MODE_MODIFY)
            return check_deps (&avail_deps, DEPS_DASDFMT_MASK, deps, DEPS_LAST, &deps_check_lock, error);
        else
            return TRUE;
    case BD_S390_TECH_PAES:
        /* pervasive encryption support requires the 'zkey' utility */
        return check_deps (&avail_deps, DEPS_ZKEY_MASK, deps, DEPS_LAST, &deps_check_lock, error);
    default:
        g_set_error_literal (error, BD_S390_ERROR, BD_S390_ERROR_TECH_UNAVAIL, "Unknown technology");
        return FALSE;
    }
}

/**
 * bd_s390_dasd_format:
 * @dasd: dasd to format
 * @extra: (nullable) (array zero-terminated=1): extra options for the formatting (right now
 *                                                 passed to the 'dasdfmt' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether dasdfmt was successful or not
 *
 * Tech category: %BD_S390_TECH_DASD-%BD_S390_TECH_MODE_MODIFY
 */
gboolean bd_s390_dasd_format (const gchar *dasd, const BDExtraArg **extra, GError **error) {
    gboolean rc = FALSE;
    const gchar *argv[8] = {"dasdfmt", "-y", "-d", "cdl", "-b", "4096", NULL, NULL};

    if (!check_deps (&avail_deps, DEPS_DASDFMT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    argv[6] = g_strdup_printf ("/dev/%s", dasd);

    rc = bd_utils_exec_and_report_error (argv, extra, error);
    g_free ((gchar *) argv[6]);
    return rc;
}

/**
 * bd_s390_dasd_needs_format:
 * @dasd: dasd to check, given as device number
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a dasd needs dasdfmt run against it
 *
 * Tech category: %BD_S390_TECH_DASD-%BD_S390_TECH_MODE_QUERY
 */
gboolean bd_s390_dasd_needs_format (const gchar *dasd, GError **error) {
    gchar status[12];
    gchar *path = NULL;
    gchar *rc = NULL;
    FILE *fd = NULL;

    path = g_strdup_printf ("/sys/bus/ccw/drivers/dasd-eckd/%s/status", dasd);
    fd = fopen (path, "r");
    g_free (path);
    if (!fd) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Error checking status of device %s; device may not exist,"
                     " or status can not be read.", dasd);
        return FALSE;
    }

    /* read 'status' value; will either be 'unformatted' or 'online' */
    memset (status, 0, sizeof (status));
    rc = fgets (status, 12, fd);
    fclose (fd);

    if (!rc) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Error checking status of device %s.", dasd);
        return FALSE;
    }

    if (g_ascii_strncasecmp (status, "unformatted", strlen (status)) == 0) {
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Device %s status is %s, needs dasdfmt.", dasd, status);
        return TRUE;
    }

    return FALSE;
}

/**
 * bd_s390_dasd_online:
 * @dasd: dasd to switch online, given as device number
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a dasd was successfully switched online
 *
 * Tech category: %BD_S390_TECH_DASD-%BD_S390_TECH_MODE_MODIFY
 */
gboolean bd_s390_dasd_online (const gchar *dasd, GError **error) {
    gboolean rc = FALSE;
    gint wrc = 0;
    gint online = 0;
    gchar *path = NULL;
    FILE *fd = NULL;
    const gchar *argv[4] = {"dasd_cio_free", "-d", dasd, NULL};
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started switching '%s' online", dasd);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    path = g_strdup_printf ("/sys/bus/ccw/drivers/dasd-eckd/%s/online", dasd);
    fd = fopen (path, "r+");
    if (!fd) {
        /* DASD might be in device ignore list; try to rm it */
        rc = bd_utils_exec_and_report_error_no_progress (argv, NULL, &l_error);
        if (!rc) {
            g_free (path);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return rc;
        }
        /* fd is NULL at this point, so try to open it */
        fd = fopen (path, "r+");
        g_free (path);

        if (!fd) {
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Could not open device %s even after removing it from"
                         " the device ignore list.", dasd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    }
    else {
        g_free (path);
    }

    /* check whether our DASD is online; if not, set it */
    online = fgetc (fd);

    if (online == EOF) {
        /* there was some error checking the 'online' status */
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Error checking if device %s is online", dasd);
        fclose (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    if (online == '1') {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "DASD device %s is already online.", dasd);
        fclose (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    else {
        /* reset file cursor before writing to it */
        rewind (fd);
        wrc = fputs ("1", fd);
    }

    fclose (fd);

    if (wrc == EOF) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Could not set DASD device %s online", dasd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_s390_dasd_is_ldl:
 * @dasd: dasd to check, whether it is LDL formatted
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a dasd is LDL formatted
 *
 * Tech category: %BD_S390_TECH_DASD-%BD_S390_TECH_MODE_QUERY
 */
gboolean bd_s390_dasd_is_ldl (const gchar *dasd, GError **error) {
    gchar *devname = NULL;
    gint f = 0;
    gint blksize = 0;
    dasd_information2_t dasd_info;

    memset (&dasd_info, 0, sizeof (dasd_info));

    /* complete the device name */
    if (g_str_has_prefix (dasd, "/dev/")) {
        devname = g_strdup (dasd);
    }
    else {
        devname = g_strdup_printf ("/dev/%s", dasd);
    }

    /* open the device */
    if ((f = open (devname, O_RDONLY)) == -1) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Unable to open device %s", devname);
        g_free (devname);
        return FALSE;
    }

    g_free (devname);

    /* check if this is a block device */
    if (ioctl (f, BLKSSZGET, &blksize) != 0) {
        close (f);
        return FALSE;
    }

    /* get some info about DASD */
    if (ioctl (f, BIODASDINFO2, &dasd_info) != 0) {
        close (f);
        return FALSE;
    }

    close (f);

    /* check we're not on an FBA DASD, since dasdfmt can't run on them */
    if (strncmp (dasd_info.type, "FBA", 3) == 0) {
        return FALSE;
    }

    /* check dasd format */
    return dasd_info.format == DASD_FORMAT_LDL;
}

/**
 * bd_s390_dasd_is_fba:
 * @dasd: dasd to check, whether it is FBA
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a dasd is FBA
 *
 * Tech category: %BD_S390_TECH_DASD-%BD_S390_TECH_MODE_QUERY
 */
gboolean bd_s390_dasd_is_fba (const gchar *dasd, GError **error) {
    gchar *devname = NULL;
    gint f = 0;
    gint blksize = 0;
    dasd_information2_t dasd_info;

    memset (&dasd_info, 0, sizeof (dasd_info));

    /* complete the device name */
    if (g_str_has_prefix (dasd, "/dev/")) {
        devname = g_strdup (dasd);
    }
    else {
        devname = g_strdup_printf ("/dev/%s", dasd);
    }

    /* open the device */
    if ((f = open (devname, O_RDONLY)) == -1) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Unable to open device %s", devname);
        g_free (devname);
        return FALSE;
    }

    g_free (devname);

    /* check if this is a block device */
    if (ioctl (f, BLKSSZGET, &blksize) != 0) {
        close (f);
        return FALSE;
    }

    /* get some info about DASD */
    if (ioctl (f, BIODASDINFO2, &dasd_info) != 0) {
        close (f);
        return FALSE;
    }

    close (f);

    /* check if we're on an FBA DASD */
    return strncmp (dasd_info.type, "FBA", 3) == 0;
}

/**
 * bd_s390_sanitize_dev_input:
 * @dev: a DASD or zFCP device number
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): a synthesized dasd or zfcp device number
 *
 * Tech category: always available
 */
gchar* bd_s390_sanitize_dev_input (const gchar *dev, GError **error) {
    gchar *tok = NULL;
    gchar *tmptok = NULL;
    gchar *prepend = NULL;
    gchar *ret = NULL;
    gchar *lcdev = NULL;

    /* first make sure we're not being played */
    if ((dev == NULL) || (!*dev)) {
        g_set_error_literal (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                             "Device number not specified or invalid");
        return NULL;
    }

    /* convert everything to lowercase */
    lcdev = g_ascii_strdown (dev, -1);

    /* we only care about the last piece of the device number, since
     * that's the only part which will need formatting */
    tok = g_strrstr (lcdev, ".");

    if (tok) {
        tmptok = tok + 1; /* want to ignore the delimiter char */
    }
    else {
        tmptok = lcdev;
    }

    /* calculate if/how much to pad tok with */
    if (strlen (tmptok) < 4)
        prepend = g_strnfill ((4 - strlen (tmptok)), '0');

    /* combine it all together */
    if (prepend == NULL) {
        ret = g_strdup_printf ("0.0.%s", tmptok);
    }
    else {
        ret = g_strdup_printf ("0.0.%s%s", prepend, tmptok);
    }
    g_free (prepend);
    g_free (lcdev);

    return ret;
}

/**
 * bd_s390_zfcp_sanitize_wwpn_input:
 * @wwpn: a zFCP WWPN identifier
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): a synthesized zFCP WWPN
 *
 * Tech category: always available
 */
gchar* bd_s390_zfcp_sanitize_wwpn_input (const gchar *wwpn, GError **error) {
    gchar *fullwwpn = NULL;
    gchar *lcwwpn = NULL;

    /* first make sure we're not being played */
    if ((wwpn == NULL) || (!*wwpn) || (strlen (wwpn) < 2)) {
        g_set_error_literal (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                             "WWPN not specified or invalid");
        return NULL;
    }

    /* convert everything to lowercase */
    lcwwpn = g_ascii_strdown (wwpn, -1);

    if (strncmp (lcwwpn, "0x", 2) == 0) {
        /* user entered a properly formatted wwpn */
        fullwwpn = g_strdup_printf ("%s", lcwwpn);
    }
    else {
        fullwwpn = g_strdup_printf ("0x%s", lcwwpn);
    }
    g_free (lcwwpn);
    return fullwwpn;
}

/**
 * bd_s390_zfcp_sanitize_lun_input:
 * @lun: a zFCP LUN identifier
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): a synthesized zFCP LUN
 *
 * Tech category: always available
 */
gchar* bd_s390_zfcp_sanitize_lun_input (const gchar *lun, GError **error) {
    gchar *lclun = NULL;
    gchar *tmplun = NULL;
    gchar *fulllun = NULL;
    gchar *prepend = NULL;
    gchar *append = NULL;

    /* first make sure we're not being played */
    if ((lun == NULL) || (!*lun) || (strlen (lun) > 18)) {
        g_set_error_literal (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                             "LUN not specified or invalid");
        return NULL;
    }

    /* convert everything to lowercase */
    lclun = g_ascii_strdown (lun, -1);

    if ((g_str_has_prefix (lclun, "0x")) && (strlen (lclun) == 18)) {
        /* user entered a properly formatted lun */
        fulllun = g_strdup_printf ("%s", lclun);
    }
    else {
        /* we need to mangle the input to make it proper. ugh. */
        if (g_str_has_prefix (lclun, "0x")) {
            /* this may seem odd, but it makes the math easier a ways down */
            tmplun = lclun + 2;
        }
        else {
            tmplun = lclun;
        }

        if (strlen (tmplun) < 4) {
            /* check if/how many zeros we pad to the left */
            prepend = g_strnfill ((4 - strlen (tmplun)), '0');
            /* check if/how many zeros we pad to the right */
            append = g_strnfill ((16 - (strlen (tmplun) + strlen (prepend))), '0');
        }
        else {
            /* didn't need to pad anything on the left; so just check if/how
             * many zeros we pad to the right */
            append = g_strnfill ((16 - (strlen (tmplun))), '0');
        }

        /* now combine everything together */
        if (prepend == NULL) {
            fulllun = g_strdup_printf ("0x%s%s", tmplun, append);
        }
        else {
            fulllun = g_strdup_printf ("0x%s%s%s", prepend, tmplun, append);
        }
    }
    g_free (lclun);
    g_free (prepend);
    g_free (append);

    return fulllun;
}

/**
 * bd_s390_zfcp_online:
 * @devno: zfcp device number
 * @wwpn: zfcp WWPN (World Wide Port Number)
 * @lun: zfcp LUN (Logical Unit Number)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a zfcp device was successfully switched online
 *
 * Tech category: %BD_S390_TECH_ZFCP-%BD_S390_TECH_MODE_MODIFY
 */
gboolean bd_s390_zfcp_online (const gchar *devno, const gchar *wwpn, const gchar *lun, GError **error) {
    gboolean boolrc = FALSE;
    gint rc = 0;
    FILE *fd = NULL;
    DIR *pdfd = NULL;
    const gchar *zfcp_cio_free[4] = {"zfcp_cio_free", "-d", devno, NULL};
    const gchar *chccwdev[4] = {"chccwdev", "-e", devno, NULL};

    gchar *zfcpsysfs = "/sys/bus/ccw/drivers/zfcp";
    gchar *online = g_strdup_printf ("%s/%s/online", zfcpsysfs, devno);
    gchar *portdir = NULL;
    gchar *unitadd = NULL;
    gchar *failed = NULL;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started switching zfcp '%s' online", devno);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    /* part 01: make sure device is available/not on device ignore list */
    fd = fopen (online, "r");
    if (!fd) {
        boolrc = bd_utils_exec_and_report_error_no_progress (zfcp_cio_free, NULL, NULL);
        if (!boolrc) {
            g_free (online);
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Could not remove device %s from device ignore list.", devno);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        /* fd is NULL at this point, so try to open it again */
        fd = fopen (online, "r");

        /* still no luck, fail */
        if (!fd) {
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Could not open device %s even after removing it from" " the device ignore list.", devno);
            g_free (online);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    }
    g_free (online);

    /* part 02: check to make sure/turn device online */
    rc = fgetc (fd);
    if (rc == EOF) {
        /* there was some error checking the 'online' status */
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Error checking if device %s is online", devno);
        fclose (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    if (rc == '1') {
        /* otherwise device's status indicates that it's already online, so
           just close the fd and proceed; we don't return because although 'online'
           status may be correct, the device may not be completely online and ready
           to use just yet, so just throw a warning. */
        fclose (fd);
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Device %s is already online", devno);
    }
    else {
        /* offline */
        fclose (fd);
        boolrc = bd_utils_exec_and_report_error_no_progress (chccwdev, NULL, NULL);
        if (!boolrc) {
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Could not set zFCP device %s online", devno);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    }

    /* part 03: set other properties to use the device */
    /* check this dir exists */
    portdir = g_strdup_printf ("%s/%s/%s", zfcpsysfs, devno, wwpn);
    pdfd = opendir (portdir);
    if (!pdfd) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "WWPN %s not found for zFCP device %s", wwpn, devno);
        g_free (portdir);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    closedir (pdfd);

    unitadd = g_strdup_printf ("%s/unit_add", portdir);
    fd = fopen (unitadd, "w");
    if (!fd) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Could not open %s", unitadd);
        g_free (unitadd);
        g_free (portdir);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    rc = fputs (lun, fd);
    if (rc == EOF) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Could not add LUN %s to WWPN %s on zFCP device %s", lun, wwpn, devno);
        g_free (unitadd);
        g_free (portdir);
        fclose (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    g_free (unitadd);
    fclose (fd);

    /* part 04: other error checking to verify device turned on properly */
    failed = g_strdup_printf ("%s/%s/failed", portdir, lun);
    fd = fopen (failed, "r");
    if (!fd) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Could not open %s", failed);
        g_free (failed);
        g_free (portdir);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    rc = fgetc (fd);
    if (rc == EOF) {
        /* there was some error checking the 'failed' status */
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Could not read failed attribute of LUN %s at WWPN %s on" " zFCP device %s", lun, wwpn, devno);
        g_free (failed);
        g_free (portdir);
        fclose (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    /* read value here is either 0 or 1; fgetc casts this from char->int, so
       subtract '0' here to get the literal read value */
    rc -= '0';
    if (rc != 0) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Failed LUN %s at WWPN %s on zFCP device %s removed again", lun, wwpn, devno);
        g_free (failed);
        g_free (portdir);
        fclose (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    /* if you haven't failed yet, you deserve this */
    g_free (failed);
    g_free (portdir);
    fclose (fd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_s390_zfcp_scsi_offline:
 * @devno: zfcp device number
 * @wwpn: zfcp WWPN (World Wide Port Number)
 * @lun: zfcp LUN (Logical Unit Number)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a LUN was successfully removed from its associated WWPN
 *
 * This function looks through /proc/scsi/scsi and manually removes LUNs from
 * associated WWPNs. zFCP devices are SCSI devices accessible over FCP protocol.
 * In z/OS the IODF (I/O definition file) contains basic information about the
 * I/O config, but WWPN and LUN configuration is done at the OS level, hence
 * this function becomes necessary when switching the device offline. This
 * particular sequence of actions is for some reason unnecessary when switching
 * the device online. Chalk it up to s390x being s390x.
 *
 * Tech category: %BD_S390_TECH_ZFCP-%BD_S390_TECH_MODE_MODIFY
 */
gboolean bd_s390_zfcp_scsi_offline (const gchar *devno, const gchar *wwpn, const gchar *lun, GError **error) {
    FILE *scsifd = NULL;
    FILE *fd = NULL;
    size_t len = 0;
    ssize_t read, rc;

    const gchar *delim = " ";
    gchar *channel = "0";
    gchar *devid = "0";
    gchar *path = "/proc/scsi/scsi";
    gchar *scsidevsysfs = "/sys/bus/scsi/devices";

    gchar *line = NULL;
    gchar *fcphbasysfs = NULL;
    gchar *fcpwwpnsysfs = NULL;
    gchar *fcplunsysfs = NULL;
    gchar *hba_path = NULL;
    gchar *wwpn_path = NULL;
    gchar *lun_path = NULL;
    gchar *scsidev = NULL;
    gchar *fcpsysfs = NULL;
    gchar *scsidel = NULL;
    gchar **tokens = NULL;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started switching zfcp scsi '%s' offline", devno);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    scsifd = fopen (path, "r");
    if (!scsifd) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Failed to open path to SCSI device: %s", path);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    while ((read = getline (&line, &len, scsifd)) != -1) {
        if (!g_str_has_prefix (line, "Host")) {
            continue;
        }

        /* tokenize line and assign certain values we'll need later */
        tokens = g_strsplit (line, delim, 8);

        scsidev = g_strdup_printf ("%s:%s:%s:%s", tokens[1] /* host */ + 4, channel, devid, tokens[7] /* fcplun */);
        scsidev = g_strchomp (scsidev);
        fcpsysfs = g_strdup_printf ("%s/%s", scsidevsysfs, scsidev);
        fcpsysfs = g_strchomp (fcpsysfs);
        g_strfreev (tokens);

        /* get HBA path value (same as device number) */
        hba_path = g_strdup_printf ("%s/hba_id", fcpsysfs);
        len = 0; /* should be zero, but re-set it just in case */
        fd = fopen (hba_path, "r");
        if (!fd) {
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Failed to open %s: %m", hba_path);
            g_free (hba_path);
            g_free (fcpsysfs);
            g_free (scsidev);
            fclose (scsifd);
            g_free (line);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        rc = getline (&fcphbasysfs, &len, fd);
        if (rc == -1) {
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Failed to read value from %s: %m", hba_path);
            fclose (fd);
            g_free (hba_path);
            g_free (fcpsysfs);
            g_free (scsidev);
            fclose (scsifd);
            g_free (line);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        g_strchomp (fcphbasysfs);
        fclose (fd);
        g_free (hba_path);

        /* get WWPN value */
        wwpn_path = g_strdup_printf ("%s/wwpn", fcpsysfs);
        len = 0;
        fd = fopen (wwpn_path, "r");
        if (!fd) {
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Failed to open %s: %m", wwpn_path);
            g_free (wwpn_path);
            g_free (fcphbasysfs);
            g_free (fcpsysfs);
            g_free (scsidev);
            fclose (scsifd);
            g_free (line);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        rc = getline (&fcpwwpnsysfs, &len, fd);
        if (rc == -1) {
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Failed to read value from %s", wwpn_path);
            g_free (wwpn_path);
            g_free (fcphbasysfs);
            g_free (fcpsysfs);
            g_free (scsidev);
            fclose (fd);
            fclose (scsifd);
            g_free (line);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        g_strchomp (fcpwwpnsysfs);
        fclose (fd);
        g_free (wwpn_path);

        /* read LUN value */
        lun_path = g_strdup_printf ("%s/fcp_lun", fcpsysfs);
        len = 0;
        fd = fopen (lun_path, "r");
        if (!fd) {
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Failed to open %s: %m", lun_path);
            g_free (lun_path);
            g_free (fcpwwpnsysfs);
            g_free (fcphbasysfs);
            g_free (fcpsysfs);
            g_free (scsidev);
            fclose (scsifd);
            g_free (line);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        rc = getline (&fcplunsysfs, &len, fd);
        if (rc == -1) {
            g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Failed to read value from %s", lun_path);
            fclose (fd);
            g_free (lun_path);
            g_free (fcpwwpnsysfs);
            g_free (fcphbasysfs);
            g_free (fcpsysfs);
            g_free (scsidev);
            fclose (scsifd);
            g_free (line);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        g_strchomp (fcplunsysfs);
        fclose (fd);
        g_free (lun_path);
        g_free (fcpsysfs);

        /* make sure read values align with expected values */
        scsidel = g_strdup_printf ("%s/%s/delete", scsidevsysfs, scsidev);
        scsidel = g_strchomp (scsidel);
        if (g_strcmp0 (fcphbasysfs, devno) == 0 && g_strcmp0 (fcpwwpnsysfs, wwpn) == 0 && g_strcmp0 (fcplunsysfs, lun) == 0) {
            fd = fopen (scsidel, "w");
            if (!fd) {
                g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                             "Failed to open %s", scsidel);
                g_free (scsidel);
                g_free (fcplunsysfs);
                g_free (fcpwwpnsysfs);
                g_free (fcphbasysfs);
                g_free (scsidev);
                fclose (scsifd);
                g_free (line);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return FALSE;
            }
            rc = fputs ("1", fd);
            if (rc == EOF) {
                g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                             "Could not write to %s", scsidel);
                fclose (fd);
                g_free (scsidel);
                g_free (fcplunsysfs);
                g_free (fcpwwpnsysfs);
                g_free (fcphbasysfs);
                g_free (scsidev);
                fclose (scsifd);
                g_free (line);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return FALSE;
            }
            fclose (fd);
        }

        g_free (scsidel);
        scsidel = NULL;
        g_free (scsidev);
        scsidev = NULL;
        g_free (fcplunsysfs);
        fcplunsysfs = NULL;
        g_free (fcpwwpnsysfs);
        fcpwwpnsysfs = NULL;
        g_free (fcphbasysfs);
        fcphbasysfs = NULL;
    }
    fclose (scsifd);
    g_free (line);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_s390_zfcp_offline:
 * @devno: zfcp device number
 * @wwpn: zfcp WWPN (World Wide Port Number)
 * @lun: zfcp LUN (Logical Unit Number)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a zfcp device was successfully switched offline
 *
 * Tech category: %BD_S390_TECH_ZFCP-%BD_S390_TECH_MODE_MODIFY
 */
gboolean bd_s390_zfcp_offline (const gchar *devno, const gchar *wwpn, const gchar *lun, GError **error) {
    gboolean success = FALSE;
    gint rc = 0;
    FILE *fd = NULL;
    glob_t luns;

    gchar *zfcpsysfs = "/sys/bus/ccw/drivers/zfcp";
    gchar *offline = NULL;
    gchar *unitrm = NULL;
    gchar *pattern = NULL;
    const gchar *chccwdev[4] = {"chccwdev", "-d", devno, NULL};
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started switching zfcp '%s' offline", devno);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    success = bd_s390_zfcp_scsi_offline (devno, wwpn, lun, NULL);
    if (!success) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Could not correctly delete SCSI device of zFCP %s with WWPN %s, LUN %s", devno, wwpn, lun);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* remove lun */
    unitrm = g_strdup_printf ("%s/%s/%s/unit_remove", zfcpsysfs, devno, wwpn);
    fd = fopen (unitrm, "w");
    if (!fd) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Failed to open %s", unitrm);
        g_free (unitrm);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    rc = fputs (lun, fd);
    if (rc == EOF) {
        fclose (fd);
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Could not remove LUN %s at WWPN %s on zFCP device %s", lun, wwpn, devno);
        g_free (unitrm);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    fclose (fd);
    g_free (unitrm);
    rc = 0;

    /* gather the luns  */
    pattern = g_strdup_printf ("%s/0x??????????????\?\?/0x????????????????", zfcpsysfs);
    rc = glob (pattern, GLOB_ONLYDIR, NULL, &luns);
    if (rc == GLOB_ABORTED || rc == GLOB_NOSPACE) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "An error occurred trying to determine if other LUNs are still associated with WWPN %s", wwpn);
        globfree (&luns);
        g_free (pattern);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    /* check if we have any matches found; if so, bail */
    if (luns.gl_pathc > 0) {
        g_set_error_literal (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                             "Not setting zFCP device offline since it still has other LUNs");
        globfree (&luns);
        g_free (pattern);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    globfree (&luns);
    g_free (pattern);
    rc = 0;

    /* offline */
    offline = g_strdup_printf ("%s/%s/online", zfcpsysfs, devno);
    success = bd_utils_exec_and_report_error_no_progress (chccwdev, NULL, NULL);
    g_free (offline);
    if (!success) {
        g_set_error (&l_error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Could not set zFCP device %s offline", devno);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_s390_zkey_generate:
 * @name: name of the secure key to generate in the secure key repository
 * @key_type: (nullable): type of the secure key to generate (e.g. "CCA-AESCIPHER") or %NULL for the zkey default
 * @keybits: size of the key in bits or 0 for the zkey default
 * @volumes: (nullable) (array zero-terminated=1): list of volumes to associate with the key, each given
 *                                                 in the "volume:dmname" format, or %NULL
 * @apqns: (nullable) (array zero-terminated=1): list of cryptographic adapters (APQNs) to associate with
 *                                               the key, each given in the "card.domain" format, or %NULL
 * @sector_size: sector size in bytes to use with dm-crypt or 0 for the system default
 * @dummy_passphrase: whether to generate and associate a dummy passphrase with the key
 * @error: (out) (optional): place to store error (if any)
 *
 * Generates a new secure key for pervasive encryption using the 'zkey' utility and stores it
 * in the secure key repository. The key is generated as an XTS key with the 'LUKS2' volume type.
 *
 * Returns: whether the secure key was successfully generated or not
 *
 * Tech category: %BD_S390_TECH_PAES-%BD_S390_TECH_MODE_CREATE
 */
gboolean bd_s390_zkey_generate (const gchar *name, const gchar *key_type, guint64 keybits, const gchar **volumes, const gchar **apqns, guint64 sector_size, gboolean dummy_passphrase, const BDExtraArg **extra, GError **error) {
    gboolean success = FALSE;
    GPtrArray *argv = NULL;

    if (!check_deps (&avail_deps, DEPS_ZKEY_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (name == NULL || *name == '\0') {
        g_set_error_literal (error, BD_S390_ERROR, BD_S390_ERROR_ZKEY,
                             "Key name must be specified");
        return FALSE;
    }

    argv = g_ptr_array_new_with_free_func (g_free);
    g_ptr_array_add (argv, g_strdup ("zkey"));
    g_ptr_array_add (argv, g_strdup ("generate"));
    g_ptr_array_add (argv, g_strdup ("--name"));
    g_ptr_array_add (argv, g_strdup (name));

    if (key_type != NULL) {
        g_ptr_array_add (argv, g_strdup ("--key-type"));
        g_ptr_array_add (argv, g_strdup (key_type));
    }
    if (keybits != 0) {
        g_ptr_array_add (argv, g_strdup ("--keybits"));
        g_ptr_array_add (argv, g_strdup_printf ("%"G_GUINT64_FORMAT, keybits));
    }
    if (volumes != NULL && *volumes != NULL) {
        /* zkey expects a single comma-separated list */
        g_ptr_array_add (argv, g_strdup ("--volumes"));
        g_ptr_array_add (argv, g_strjoinv (",", (gchar **) volumes));
    }
    if (apqns != NULL && *apqns != NULL) {
        /* zkey expects a single comma-separated list */
        g_ptr_array_add (argv, g_strdup ("--apqns"));
        g_ptr_array_add (argv, g_strjoinv (",", (gchar **) apqns));
    }
    if (sector_size != 0) {
        g_ptr_array_add (argv, g_strdup ("--sector-size"));
        g_ptr_array_add (argv, g_strdup_printf ("%"G_GUINT64_FORMAT, sector_size));
    }
    if (dummy_passphrase)
        g_ptr_array_add (argv, g_strdup ("--gen-dummy-passphrase"));

    /* pervasive encryption of LUKS2 volumes always uses XTS secure keys */
    g_ptr_array_add (argv, g_strdup ("--xts"));
    g_ptr_array_add (argv, g_strdup ("--volume-type"));
    g_ptr_array_add (argv, g_strdup ("LUKS2"));

    g_ptr_array_add (argv, NULL);

    success = bd_utils_exec_and_report_error ((const gchar **) argv->pdata, extra, error);
    g_ptr_array_free (argv, TRUE);

    return success;
}

/**
 * bd_s390_zkey_info_free: (skip)
 * @info: (nullable): %BDS390ZkeyInfo to free
 *
 * Frees @info.
 */
void bd_s390_zkey_info_free (BDS390ZkeyInfo *info) {
    if (info == NULL)
        return;

    g_free (info->name);
    g_free (info->description);
    g_free (info->key_type);
    g_strfreev (info->volumes);
    g_strfreev (info->apqns);
    g_free (info->key_file_name);
    g_free (info->volume_type);
    g_free (info->dummy_passphrase);
    g_free (info);
}

/**
 * bd_s390_zkey_info_copy: (skip)
 * @info: (nullable): %BDS390ZkeyInfo to copy
 *
 * Creates a new copy of @info.
 */
BDS390ZkeyInfo* bd_s390_zkey_info_copy (BDS390ZkeyInfo *info) {
    if (info == NULL)
        return NULL;

    BDS390ZkeyInfo *new_info = g_new0 (BDS390ZkeyInfo, 1);

    new_info->name = g_strdup (info->name);
    new_info->description = g_strdup (info->description);
    new_info->secure_key_size = info->secure_key_size;
    new_info->clear_key_size = info->clear_key_size;
    new_info->xts = info->xts;
    new_info->key_type = g_strdup (info->key_type);
    new_info->volumes = g_strdupv (info->volumes);
    new_info->apqns = g_strdupv (info->apqns);
    new_info->key_file_name = g_strdup (info->key_file_name);
    new_info->sector_size = info->sector_size;
    new_info->volume_type = g_strdup (info->volume_type);
    new_info->dummy_passphrase = g_strdup (info->dummy_passphrase);

    return new_info;
}

/**
 * bd_s390_zkey_list:
 * @name: (nullable): name of a single secure key to get information about or %NULL to list all keys
 * @error: (out) (optional): place to store error (if any)
 *
 * Lists the secure keys stored in the secure key repository using the 'zkey' utility. If @name
 * is given, only the information about the matching key is returned.
 *
 * Returns: (array zero-terminated=1) (transfer full): information about the secure keys in the
 *          repository (an empty list if there are none) or %NULL in case of error
 *
 * Tech category: %BD_S390_TECH_PAES-%BD_S390_TECH_MODE_QUERY
 */
BDS390ZkeyInfo** bd_s390_zkey_list (const gchar *name, GError **error) {
    const gchar *argv[5] = {"zkey", "list", NULL, NULL, NULL};
    guint next = 2;
    gchar *output = NULL;
    gchar *stderr_data = NULL;
    gint status = 0;
    gboolean success = FALSE;
    GPtrArray *keys = NULL;
    gchar **lines = NULL;
    BDS390ZkeyInfo *cur_info = NULL;

    if (!check_deps (&avail_deps, DEPS_ZKEY_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    if (name != NULL && *name != '\0') {
        argv[next++] = "--name";
        argv[next++] = name;
    }

    /* not using bd_utils_exec_and_capture_output because it treats an empty output
       (i.e. no keys in the repository) as an error */
    success = bd_utils_exec_and_capture_output_no_progress (argv, NULL, &output, &stderr_data, &status, error);
    if (!success) {
        g_free (output);
        g_free (stderr_data);
        return NULL;
    }
    if (status != 0) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_ZKEY,
                     "Failed to list secure keys: %s", stderr_data ? stderr_data : "");
        g_free (output);
        g_free (stderr_data);
        return NULL;
    }
    g_free (stderr_data);

    keys = g_ptr_array_new ();

    lines = g_strsplit (output ? output : "", "\n", -1);
    g_free (output);

    for (gchar **line_p = lines; *line_p != NULL; line_p++) {
        gchar *colon = NULL;
        gchar *label = NULL;
        gchar *value = NULL;

        /* split the line on the first ':' -- the label never contains a colon while
           some values (e.g. Volumes) do; lines without a colon are separators, blank
           lines or continuation lines (e.g. the second line of Verification pattern) */
        colon = strchr (*line_p, ':');
        if (colon == NULL)
            continue;

        label = g_strndup (*line_p, colon - *line_p);
        label = g_strstrip (label);
        value = g_strdup (colon + 1);
        value = g_strstrip (value);

        if (g_strcmp0 (label, "Key") == 0) {
            /* the "Key" line starts a new key record */
            cur_info = g_new0 (BDS390ZkeyInfo, 1);
            cur_info->name = g_strdup (value);
            g_ptr_array_add (keys, cur_info);
        } else if (cur_info == NULL) {
            /* a field line before any "Key" line -- ignore it */
        } else if (g_strcmp0 (label, "Description") == 0) {
            cur_info->description = g_strdup (value);
        } else if (g_strcmp0 (label, "Secure key size") == 0) {
            cur_info->secure_key_size = g_ascii_strtoull (value, NULL, 0);
        } else if (g_strcmp0 (label, "Clear key size") == 0) {
            cur_info->clear_key_size = g_ascii_strtoull (value, NULL, 0);
        } else if (g_strcmp0 (label, "XTS type key") == 0) {
            cur_info->xts = (g_ascii_strcasecmp (value, "Yes") == 0);
        } else if (g_strcmp0 (label, "Key type") == 0) {
            cur_info->key_type = g_strdup (value);
        } else if (g_strcmp0 (label, "Volumes") == 0) {
            cur_info->volumes = g_strsplit (value, ",", -1);
        } else if (g_strcmp0 (label, "APQNs") == 0) {
            cur_info->apqns = g_strsplit (value, ",", -1);
        } else if (g_strcmp0 (label, "Key file name") == 0) {
            cur_info->key_file_name = g_strdup (value);
        } else if (g_strcmp0 (label, "Sector size") == 0) {
            /* "(system default)" is reported as 0 */
            cur_info->sector_size = g_ascii_strtoull (value, NULL, 0);
        } else if (g_strcmp0 (label, "Volume type") == 0) {
            cur_info->volume_type = g_strdup (value);
        } else if (g_strcmp0 (label, "Dummy passphrase") == 0) {
            /* "(none)" means no dummy passphrase is set, otherwise it is a path to the passphrase file */
            if (g_strcmp0 (value, "(none)") != 0)
                cur_info->dummy_passphrase = g_strdup (value);
        }

        g_free (label);
        g_free (value);
    }

    g_strfreev (lines);

    g_ptr_array_add (keys, NULL);
    return (BDS390ZkeyInfo **) g_ptr_array_free (keys, FALSE);
}

/**
 * bd_s390_zkey_remove:
 * @name: name of the secure key to remove from the secure key repository
 * @error: (out) (optional): place to store error (if any)
 *
 * Removes the secure key @name from the secure key repository using the 'zkey' utility.
 *
 * Returns: whether the secure key was successfully removed or not
 *
 * Tech category: %BD_S390_TECH_PAES-%BD_S390_TECH_MODE_MODIFY
 */
gboolean bd_s390_zkey_remove (const gchar *name, GError **error) {
    /* --force suppresses the interactive y/n confirmation */
    const gchar *argv[6] = {"zkey", "remove", "--name", name, "--force", NULL};

    if (!check_deps (&avail_deps, DEPS_ZKEY_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (name == NULL || *name == '\0') {
        g_set_error_literal (error, BD_S390_ERROR, BD_S390_ERROR_ZKEY,
                             "Key name must be specified");
        return FALSE;
    }

    return bd_utils_exec_and_report_error (argv, NULL, error);
}
