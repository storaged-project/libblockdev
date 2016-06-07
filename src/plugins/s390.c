/*
 * Copyright (C) 2015  Red Hat, Inc.
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
 * Author: Samantha N. Bueno <sbueno@redhat.com>
 */

#include <glib.h>
#include <linux/fs.h>
#include <stdio.h>
#include <string.h>
#include <utils.h>
#include <asm/dasd.h>
#include <s390utils/vtoc.h>

#include "s390.h"


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


/**
 * check: (skip)
 */
gboolean check() {
    GError *error = NULL;
    /* dasdfmt doesn't return version info */
    gboolean ret = bd_utils_check_util_version ("dasdfmt", NULL, NULL, NULL, &error);
    if (!ret && error) {
        g_warning("Cannot load the s390 plugin: %s" , error->message);
        g_clear_error (&error);
    }
    return ret;
}

/**
 * bd_s390_dasd_format:
 * @dasd: dasd to format
 * @extra: (allow-none) (array zero-terminated=1): extra options for the formatting (right now
 *                                                 passed to the 'dasdfmt' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether dasdfmt was successful or not
 */
gboolean bd_s390_dasd_format (const gchar *dasd, const BDExtraArg **extra, GError **error) {
    gboolean rc = FALSE;
    gchar *dev = g_strdup_printf ("/dev/%s", dasd);
    const gchar *argv[8] = {"/sbin/dasdfmt", "-y", "-d", "cdl", "-b", "4096", dev, NULL};

    rc = bd_utils_exec_and_report_error (argv, extra, error);
    g_free (dev);
    return rc;
}

/**
 * bd_s390_dasd_needs_format:
 * @dasd: dasd to check, given as device number
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a dasd needs dasdfmt run against it
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
        g_clear_error (error);
        return FALSE;
    }

    /* read 'status' value; will either be 'unformatted' or 'online' */
    memset (status, 0, sizeof(status));
    rc = fgets (status, 12, fd);
    fclose(fd);

    if (!rc) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Error checking status of device %s.", dasd);
        g_clear_error (error);
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
 */
gboolean bd_s390_dasd_online (const gchar *dasd, GError **error) {
    gboolean rc = FALSE;
    gint wrc = 0;
    gint online = 0;
    gchar *path = NULL;
    FILE *fd = NULL;
    const gchar *argv[4] = {"/usr/sbin/dasd_cio_free", "-d", dasd, NULL};

    path = g_strdup_printf ("/sys/bus/ccw/drivers/dasd-eckd/%s/online", dasd);
    fd = fopen(path, "r+");
    if (!fd) {
        /* DASD might be in device ignore list; try to rm it */
        rc = bd_utils_exec_and_report_error (argv, NULL, error);

        if (!rc) {
            g_free (path);
            return rc;
        }
        /* fd is NULL at this point, so try to open it */
        fd = fopen(path, "r+");
        g_free (path);

        if (!fd) {
            g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                         "Could not open device %s even after removing it from"
                         " the device ignore list.", dasd);
            g_clear_error (error);
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
        return FALSE;
    }
    if (online == 1) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "DASD device %s is already online.", dasd);
        fclose(fd);
        return FALSE;
    }
    else {
        /* reset file cursor before writing to it */
        rewind (fd);
        wrc = fputs("1", fd);
    }

    fclose(fd);

    if (wrc == EOF) {
        g_warning ("Could not set DASD device %s online", dasd);
        fclose(fd);
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_s390_dasd_is_ldl:
 * @dasd: dasd to check, whether it is LDL formatted
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a dasd is LDL formatted
 */
gboolean bd_s390_dasd_is_ldl (const gchar *dasd, GError **error) {
    gchar *devname = NULL;
    gint f = 0;
    gint blksize = 0;
    dasd_information2_t dasd_info;

    memset(&dasd_info, 0, sizeof(dasd_info));
    devname = g_strdup_printf ("/dev/%s", dasd);

    if ((f = open(devname, O_RDONLY)) == -1) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "Unable to open device %s", devname);
        g_free (devname);
        return FALSE;
    }

    g_free (devname);

    if (ioctl(f, BLKSSZGET, &blksize) != 0) {
        close(f);
        return FALSE;
    }

    if (ioctl(f, BIODASDINFO2, &dasd_info) != 0) {
        close(f);
        return FALSE;
    }

    close(f);

    /* check we're not on an FBA DASD, since dasdfmt can't run on them */
    if (strncmp(dasd_info.type, "FBA", 3) == 0) {
        return FALSE;
    }

    /* check dasd volume label; "VOL1" is a CDL formatted DASD, won't require formatting */
    if (dasd_info.format == DASD_FORMAT_CDL) {
        return FALSE;
    }
    else {
        return TRUE;
    }
}

/**
 * bd_s390_sanitize_dev_input:
 * @dev a DASD or zFCP device number
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): a synthesized dasd or zfcp device number
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
                     "You have not specified a device number or the number is invalid");
        g_clear_error (error);
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
 */
gchar* bd_s390_zfcp_sanitize_wwpn_input (const gchar *wwpn, GError **error) {
    gchar *fullwwpn = NULL;
    gchar *lcwwpn = NULL;

    /* first make sure we're not being played */
    if ((wwpn == NULL) || (!*wwpn)) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "You have not specified a WWPN or the WWPN is invalid");
        g_clear_error (error);
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
 */
gchar* bd_s390_zfcp_sanitize_lun_input (const gchar *lun, GError **error) {
    gchar *lclun = NULL;
    gchar *tmplun = NULL;
    gchar *fulllun = NULL;
    gchar *prepend = NULL;
    gchar *append = NULL;

    /* first make sure we're not being played */
    if ((lun == NULL) || (!*lun)) {
        g_set_error (error, BD_S390_ERROR, BD_S390_ERROR_DEVICE,
                     "You have not specified a LUN or the LUN is invalid");
        g_clear_error (error);
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
