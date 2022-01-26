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
#define DEPS_LAST 1

static const UtilDep deps[DEPS_LAST] = {
    /* dasdfmt doesn't return version info */
    {"dasdfmt", NULL, NULL, NULL},
};


/**
 * bd_s390_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_s390_check_deps (void) {
    GError *error = NULL;
    guint i = 0;
    gboolean status = FALSE;
    gboolean ret = TRUE;

    for (i=0; i < DEPS_LAST; i++) {
        status = bd_utils_check_util_version (deps[i].name, deps[i].version,
                                              deps[i].ver_arg, deps[i].ver_regexp, &error);
        if (!status)
            g_warning ("%s", error->message);
        else
            g_atomic_int_or (&avail_deps, 1 << i);
        g_clear_error (&error);
        ret = ret && status;
    }

    if (!ret)
        g_warning("Cannot load the s390 plugin");

    return ret;
}

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
    /* nothing to do here */
}

/**
 * bd_s390_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDS390TechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
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
    default:
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_TECH_UNAVAIL, "Unknown technology");
        return FALSE;
    }
}

/**
 * bd_s390_dasd_format:
 * @dasd: dasd to format
 * @extra: (allow-none) (array zero-terminated=1): extra options for the formatting (right now
 *                                                 passed to the 'dasdfmt' utility)
 * @error: (out): place to store error (if any)
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
 * @error: (out): place to store error (if any)
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
    fd = fopen(path, "r");
    g_free (path);
    if (!fd) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Error checking status of device %s; device may not exist,"
                     " or status can not be read.", dasd);
        return FALSE;
    }

    /* read 'status' value; will either be 'unformatted' or 'online' */
    memset (status, 0, sizeof(status));
    rc = fgets (status, 12, fd);
    fclose(fd);

    if (!rc) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Error checking status of device %s.", dasd);
        return FALSE;
    }

    if (g_ascii_strncasecmp (status, "unformatted", strlen(status)) == 0) {
        g_warning ("Device %s status is %s, needs dasdfmt.", dasd, status);
        g_clear_error (error);
        return TRUE;
    }

    return FALSE;
}

/**
 * bd_s390_dasd_online:
 * @dasd: dasd to switch online, given as device number
 * @error: (out): place to store error (if any)
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

    msg = g_strdup_printf ("Started switching '%s' online", dasd);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    path = g_strdup_printf ("/sys/bus/ccw/drivers/dasd-eckd/%s/online", dasd);
    fd = fopen(path, "r+");
    if (!fd) {
        /* DASD might be in device ignore list; try to rm it */
        rc = bd_utils_exec_and_report_error_no_progress (argv, NULL, error);

        if (!rc) {
            g_free (path);
            bd_utils_report_finished (progress_id, (*error)->message);
            return rc;
        }
        /* fd is NULL at this point, so try to open it */
        fd = fopen(path, "r+");
        g_free (path);

        if (!fd) {
            g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Could not open device %s even after removing it from"
                         " the device ignore list.", dasd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }
    else {
        g_free (path);
    }

    /* check whether our DASD is online; if not, set it */
    online = fgetc(fd);

    if (online == EOF) {
        /* there was some error checking the 'online' status */
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Error checking if device %s is online", dasd);
        fclose(fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    if (online == 1) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "DASD device %s is already online.", dasd);
        fclose(fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    else {
        /* reset file cursor before writing to it */
        rewind (fd);
        wrc = fputs("1", fd);
    }

    fclose(fd);

    if (wrc == EOF) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Could not set DASD device %s online", dasd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_s390_dasd_is_ldl:
 * @dasd: dasd to check, whether it is LDL formatted
 * @error: (out): place to store error (if any)
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

    memset(&dasd_info, 0, sizeof(dasd_info));

    /* complete the device name */
    if (g_str_has_prefix (dasd, "/dev/")) {
        devname = g_strdup (dasd);
    }
    else {
        devname = g_strdup_printf ("/dev/%s", dasd);
    }

    /* open the device */
    if ((f = open(devname, O_RDONLY)) == -1) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Unable to open device %s", devname);
        g_free (devname);
        return FALSE;
    }

    g_free (devname);

    /* check if this is a block device */
    if (ioctl(f, BLKSSZGET, &blksize) != 0) {
        close(f);
        return FALSE;
    }

    /* get some info about DASD */
    if (ioctl(f, BIODASDINFO2, &dasd_info) != 0) {
        close(f);
        return FALSE;
    }

    close(f);

    /* check we're not on an FBA DASD, since dasdfmt can't run on them */
    if (strncmp(dasd_info.type, "FBA", 3) == 0) {
        return FALSE;
    }

    /* check dasd format */
    return dasd_info.format == DASD_FORMAT_LDL;
}

/**
 * bd_s390_dasd_is_fba:
 * @dasd: dasd to check, whether it is FBA
 * @error: (out): place to store error (if any)
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

    memset(&dasd_info, 0, sizeof(dasd_info));

    /* complete the device name */
    if (g_str_has_prefix (dasd, "/dev/")) {
        devname = g_strdup (dasd);
    }
    else {
        devname = g_strdup_printf ("/dev/%s", dasd);
    }

    /* open the device */
    if ((f = open(devname, O_RDONLY)) == -1) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Unable to open device %s", devname);
        g_free (devname);
        return FALSE;
    }

    g_free (devname);

    /* check if this is a block device */
    if (ioctl(f, BLKSSZGET, &blksize) != 0) {
        close(f);
        return FALSE;
    }

    /* get some info about DASD */
    if (ioctl(f, BIODASDINFO2, &dasd_info) != 0) {
        close(f);
        return FALSE;
    }

    close(f);

    /* check if we're on an FBA DASD */
    return strncmp(dasd_info.type, "FBA", 3) == 0;
}

/**
 * bd_s390_sanitize_dev_input:
 * @dev a DASD or zFCP device number
 * @error: (out): place to store error (if any)
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
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Device number not specified or invalid");
        return NULL;
    }

    /* convert everything to lowercase */
    lcdev = g_ascii_strdown (dev, -1);

    /* we only care about the last piece of the device number, since
     * that's the only part which will need formatting */
    tok = g_strrstr(lcdev, ".");

    if (tok) {
        tmptok = tok + 1; /* want to ignore the delimiter char */
    }
    else {
        tmptok = lcdev;
    }

    /* calculate if/how much to pad tok with */
    if (strlen(tmptok) < 4)
        prepend = g_strnfill((4 - strlen(tmptok)), '0');

    /* combine it all together */
    if (prepend == NULL) {
        ret = g_strdup_printf("0.0.%s", tmptok);
    }
    else {
        ret = g_strdup_printf("0.0.%s%s", prepend, tmptok);
    }
    g_free (prepend);
    g_free (lcdev);

    return ret;
}

/**
 * bd_s390_zfcp_sanitize_wwpn_input:
 * @dev a zFCP WWPN identifier
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): a synthesized zFCP WWPN
 *
 * Tech category: always available
 */
gchar* bd_s390_zfcp_sanitize_wwpn_input (const gchar *wwpn, GError **error) {
    gchar *fullwwpn = NULL;
    gchar *lcwwpn = NULL;

    /* first make sure we're not being played */
    if ((wwpn == NULL) || (!*wwpn) || (strlen(wwpn) < 2)) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "WWPN not specified or invalid");
        return NULL;
    }

    /* convert everything to lowercase */
    lcwwpn = g_ascii_strdown (wwpn, -1);

    if (strncmp(lcwwpn, "0x", 2) == 0) {
        /* user entered a properly formatted wwpn */
        fullwwpn = g_strdup_printf("%s", lcwwpn);
    }
    else {
        fullwwpn = g_strdup_printf("0x%s", lcwwpn);
    }
    g_free (lcwwpn);
    return fullwwpn;
}

/**
 * bd_s390_zfcp_sanitize_lun_input:
 * @dev a zFCP LUN identifier
 * @error: (out): place to store error (if any)
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
    if ((lun == NULL) || (!*lun) || (strlen(lun) > 18)) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "LUN not specified or invalid");
        return NULL;
    }

    /* convert everything to lowercase */
    lclun = g_ascii_strdown (lun, -1);

    if ((g_str_has_prefix(lclun, "0x")) && (strlen(lclun) == 18)) {
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

        if (strlen(tmplun) < 4) {
            /* check if/how many zeros we pad to the left */
            prepend = g_strnfill((4 - strlen(tmplun)), '0');
            /* check if/how many zeros we pad to the right */
            append = g_strnfill((16 - (strlen(tmplun) + strlen(prepend))), '0');
        }
        else {
            /* didn't need to pad anything on the left; so just check if/how
             * many zeros we pad to the right */
            append = g_strnfill((16 - (strlen(tmplun))), '0');
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
 * @error: (out): place to store error (if any)
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

    msg = g_strdup_printf ("Started switching zfcp '%s' online", devno);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    /* part 01: make sure device is available/not on device ignore list */
    fd = fopen (online, "r");
    if (!fd) {
        boolrc = bd_utils_exec_and_report_error_no_progress (zfcp_cio_free, NULL, error);

        if (!boolrc) {
            fclose (fd);
            g_free (online);
            g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Could not remove device %s from device ignore list.", devno);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        /* fd is NULL at this point, so try to open it again */
        fd = fopen (online, "r");

        /* still no luck, fail */
        if (!fd) {
            g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Could not open device %s even after removing it from" " the device ignore list.", devno);
            g_free (online);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }
    g_free (online);

    /* part 02: check to make sure/turn device online */
    rc = fgetc (fd);
    if (rc == EOF) {
        /* there was some error checking the 'online' status */
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Error checking if device %s is online", devno);
        fclose (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    if (rc == 1) {
        /**
         * otherwise device's status indicates that it's already online, so
         * just close the fd and proceed; we don't return because although 'online'
         * status may be correct, the device may not be completely online and ready
         * to use just yet, so just throw a warning.
         */
        fclose (fd);
        g_warning ("Device %s is already online", devno);
    }
    else {
        /* offline */
        fclose (fd);
        boolrc = bd_utils_exec_and_report_error_no_progress (chccwdev, NULL, error);
        if (!boolrc) {
            g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Could not set zFCP device %s online", devno);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    /* part 03: set other properties to use the device */
    /* check this dir exists */
    portdir = g_strdup_printf ("%s/%s/%s", zfcpsysfs, devno, wwpn);
    pdfd = opendir (portdir);
    if (!pdfd) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "WWPN %s not found for zFCP device %s", wwpn, devno);
        g_free (portdir);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    closedir (pdfd);

    unitadd = g_strdup_printf ("%s/unit_add", portdir);
    fd = fopen (unitadd, "w");
    if (!fd) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Could not open %s", unitadd);
        g_free (unitadd);
        g_free (portdir);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    rc = fputs (lun, fd);
    if (rc == EOF) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Could not add LUN %s to WWPN %s on zFCP device %s", lun, wwpn, devno);
        g_free (unitadd);
        g_free (portdir);
        fclose (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    g_free (unitadd);
    fclose (fd);

    /* part 04: other error checking to verify device turned on properly */
    failed = g_strdup_printf ("%s/%s/failed", portdir, lun);
    fd = fopen (failed, "r");
    if (!fd) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Could not open %s", failed);
        g_free (failed);
        g_free (portdir);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    rc = fgetc (fd);
    if (rc == EOF) {
        /* there was some error checking the 'failed' status */
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_IO,
                     "Could not read failed attribute of LUN %s at WWPN %s on" " zFCP device %s", lun, wwpn, devno);
        g_free (failed);
        g_free (portdir);
        fclose (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    /**
     * read value here is either 0 or 1; fgetc casts this from char->int, so
     * subtract '0' here to get the literal read value
     */
    rc -= '0';
    if (rc != 0) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Failed LUN %s at WWPN %s on zFCP device %s removed again", lun, wwpn, devno);
        g_free (failed);
        g_free (portdir);
        fclose (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
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
 * bd_s390_zfcp_scsi_offline
 *
 * @devno: zfcp device number
 * @wwpn: zfcp WWPN (World Wide Port Number)
 * @lun: zfcp LUN (Logical Unit Number)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a LUN was successfully removed from its associated WWPN
 *
 * This function looks through /proc/scsi/scsi and manually removes LUNs from
 * associated WWPNs. zFCP devices are SCSI devices accessible over FCP protocol.
 * In z/OS the IODF (I/O definition file) contains basic information about the
 * I/O config, but WWPN and LUN configuration is done at the OS level, hence
 * this function becomes necessary when switching the device offline. This
 * particular sequence of actions is for some reason unnecessary when switching
 * the device offline. Chalk it up to s390x being s390x.
 *
 * Tech category: %BD_S390_TECH_ZFCP-%BD_S390_TECH_MODE_MODIFY
 */
gboolean bd_s390_zfcp_scsi_offline(const gchar *devno, const gchar *wwpn, const gchar *lun, GError **error) {
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

    msg = g_strdup_printf ("Started switching zfcp scsi '%s' offline", devno);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    scsifd = fopen (path, "r");
    if (!scsifd) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Failed to open path to SCSI device: %s", path);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    while ((read = getline(&line, &len, scsifd)) != -1) {
        if (!g_str_has_prefix(line, "Host")) {
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
        rc = getline (&fcphbasysfs, &len, fd);
        if (rc == -1) {
            g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Failed to read value from %s", hba_path);
            fclose (fd);
            g_free (hba_path);
            g_free (fcpsysfs);
            g_free (scsidev);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        fclose (fd);
        g_free (hba_path);

        /* get WWPN value */
        wwpn_path = g_strdup_printf ("%s/wwpn", fcpsysfs);
        len = 0;
        fd = fopen (wwpn_path, "r");
        rc = getline (&fcpwwpnsysfs, &len, fd);
        if (rc == -1) {
            g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Failed to read value from %s", wwpn_path);
            g_free (wwpn_path);
            g_free (fcphbasysfs);
            g_free (fcpsysfs);
            g_free (scsidev);
            fclose (fd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        fclose(fd);
        g_free (wwpn_path);

        /* read LUN value */
        lun_path = g_strdup_printf ("%s/fcp_lun", fcpsysfs);
        len = 0;
        fd = fopen (lun_path, "r");
        rc = getline (&fcplunsysfs, &len, fd);
        if (rc == -1) {
            g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Failed to read value from %s", lun_path);
            fclose (fd);
            g_free (lun_path);
            g_free (fcpwwpnsysfs);
            g_free (fcphbasysfs);
            g_free (fcpsysfs);
            g_free (scsidev);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        fclose(fd);
        g_free (lun_path);
        g_free (fcpsysfs);

        /* make sure read values align with expected values */
        scsidel = g_strdup_printf ("%s/%s/delete", scsidevsysfs, scsidev);
        scsidel = g_strchomp (scsidel);
        if ((fcphbasysfs == devno) && (fcpwwpnsysfs == wwpn) && (fcplunsysfs == lun)) {
            fd = fopen (scsidel, "w");
            if (!fd) {
                g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                             "Failed to open %s", scsidel);
                g_free (scsidel);
                g_free (fcplunsysfs);
                g_free (fcpwwpnsysfs);
                g_free (fcphbasysfs);
                g_free (scsidev);
                bd_utils_report_finished (progress_id, (*error)->message);
                return FALSE;
            }
            rc = fputs ("1", fd);
            if (rc == EOF) {
                g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                             "Could not write to %s", scsidel);
                fclose (fd);
                g_free (scsidel);
                g_free (fcplunsysfs);
                g_free (fcpwwpnsysfs);
                g_free (fcphbasysfs);
                g_free (scsidev);
                bd_utils_report_finished (progress_id, (*error)->message);
                return FALSE;
            }
            fclose (fd);
        }
    }
    fclose (scsifd);
    g_free (scsidel);
    g_free (fcplunsysfs);
    g_free (fcpwwpnsysfs);
    g_free (fcphbasysfs);
    g_free (scsidev);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_s390_zfcp_offline:
 * @devno: zfcp device number
 * @wwpn: zfcp WWPN (World Wide Port Number)
 * @lun: zfcp LUN (Logical Unit Number)
 * @error: (out): place to store error (if any)
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

    msg = g_strdup_printf ("Started switching zfcp '%s' offline", devno);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    success = bd_s390_zfcp_scsi_offline(devno, wwpn, lun, error);
    if (!success) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Could not correctly delete SCSI device of zFCP %s with WWPN %s, LUN %s", devno, wwpn, lun);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* remove lun */
    unitrm = g_strdup_printf ("%s/%s/%s/unit_remove", zfcpsysfs, devno, wwpn);
    fd = fopen (unitrm, "w");
    if (!fd) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Failed to open %s", unitrm);
        g_free (unitrm);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    rc = fputs (lun, fd);
    if (rc == EOF) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Could not remove LUN %s at WWPN %s on zFCP device %s", lun, wwpn, devno);
        g_free (unitrm);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    fclose (fd);
    g_free (unitrm);
    rc = 0;

    /* gather the luns  */
    pattern = g_strdup_printf ("%s/0x??????????????\?\?/0x????????????????", zfcpsysfs);
    rc = glob (pattern, GLOB_ONLYDIR, NULL, &luns);
    if (rc == GLOB_ABORTED || rc == GLOB_NOSPACE) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "An error occurred trying to determine if other LUNs are still associated with WWPN %s", wwpn);
        globfree (&luns);
        g_free (pattern);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    /* check if we have any matches found; if so, bail */
    if (luns.gl_pathc > 0) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Not setting zFCP device offline since it still has other LUNs");
        globfree (&luns);
        g_free (pattern);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    globfree (&luns);
    g_free (pattern);
    rc = 0;

    /* offline */
    offline = g_strdup_printf ("%s/%s/online", zfcpsysfs, devno);
    success = bd_utils_exec_and_report_error_no_progress (chccwdev, NULL, error);
    g_free (offline);
    if (!success) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Could not set zFCP device %s online", devno);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}
