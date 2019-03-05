/*
 * Copyright (C) 2018  Red Hat, Inc.
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
#include <blockdev/utils.h>
#include <ndctl/libndctl.h>
#include <uuid.h>
#include <string.h>

#include "nvdimm.h"
#include "check_deps.h"

/**
 * SECTION: nvdimm
 * @short_description: plugin for operations with nvdimm space
 * @title: NVDIMM
 * @include: nvdimm.h
 *
 * A plugin for operations with NVDIMM devices.
 */

/**
 * bd_nvdimm_error_quark: (skip)
 */
GQuark bd_nvdimm_error_quark (void) {
    return g_quark_from_static_string ("g-bd-nvdimm-error-quark");
}

void bd_nvdimm_namespace_info_free (BDNVDIMMNamespaceInfo *info) {
    if (info == NULL)
        return;

    g_free (info->dev);
    g_free (info->uuid);
    g_free (info->blockdev);
    g_free (info);
}

BDNVDIMMNamespaceInfo* bd_nvdimm_namespace_info_copy (BDNVDIMMNamespaceInfo *info) {
    if (info == NULL)
        return NULL;

    BDNVDIMMNamespaceInfo *new_info = g_new0 (BDNVDIMMNamespaceInfo, 1);

    new_info->dev = g_strdup (info->dev);
    new_info->mode = info->mode;
    new_info->size = info->size;
    new_info->uuid = g_strdup (info->uuid);
    new_info->sector_size = info->sector_size;
    new_info->blockdev = g_strdup (info->blockdev);
    new_info->enabled = info->enabled;

    return new_info;
}


static const gchar * const mode_str[BD_NVDIMM_NAMESPACE_MODE_UNKNOWN+1] = {"raw", "sector", "memory", "dax", "fsdax", "devdax", "unknown"};

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_NDCTL 0
#define DEPS_NDCTL_MASK (1 << DEPS_NDCTL)
#define DEPS_LAST 1

static const UtilDep deps[DEPS_LAST] = {
    {"ndctl", NULL, NULL, NULL},
};

/**
 * bd_nvdimm_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_nvdimm_check_deps (void) {
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
        g_warning("Cannot load the NVDIMM plugin");

    return ret;
}

/**
 * bd_nvdimm_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_nvdimm_init (void) {
    /* nothing to do here */
    return TRUE;
}

/**
 * bd_nvdimm_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_nvdimm_close (void) {
    /* nothing to do here */
    return;
}

#define UNUSED __attribute__((unused))

/**
 * bd_nvdimm_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDNVDIMMTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_nvdimm_is_tech_avail (BDNVDIMMTech tech, guint64 mode, GError **error) {
  /* all tech-mode combinations are supported by this implementation of the
     plugin, namespace reconfigure requires the 'ndctl' utility */

    if (tech == BD_NVDIMM_TECH_NAMESPACE) {
        if (mode & BD_NVDIMM_TECH_MODE_RECONFIGURE)
            return check_deps (&avail_deps, DEPS_NDCTL_MASK, deps, DEPS_LAST, &deps_check_lock, error);
        else
            return TRUE;
    } else {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_TECH_UNAVAIL, "Unknown technology");
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_nvdimm_namespace_get_mode_from_str:
 * @mode_str: string representation of mode
 * @error: (out): place to store error (if any)
 *
 * Returns: mode matching the @mode_str given or %BD_NVDIMM_NAMESPACE_MODE_UNKNOWN in case of no match
 *
 * Tech category: always available
 */
BDNVDIMMNamespaceMode bd_nvdimm_namespace_get_mode_from_str (const gchar *mode_str, GError **error) {
    if (g_strcmp0 (mode_str, "raw") == 0)
        return BD_NVDIMM_NAMESPACE_MODE_RAW;
    else if (g_strcmp0 (mode_str, "sector") == 0)
        return BD_NVDIMM_NAMESPACE_MODE_SECTOR;
    else if (g_strcmp0 (mode_str, "memory") == 0)
        return BD_NVDIMM_NAMESPACE_MODE_MEMORY;
    else if (g_strcmp0 (mode_str, "dax") == 0)
        return BD_NVDIMM_NAMESPACE_MODE_DAX;
    else if (g_strcmp0 (mode_str, "fsdax") == 0)
        return BD_NVDIMM_NAMESPACE_MODE_FSDAX;
    else if (g_strcmp0 (mode_str, "devdax") == 0)
        return BD_NVDIMM_NAMESPACE_MODE_DEVDAX;
    else {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_MODE_INVAL,
                     "Invalid mode given: '%s'", mode_str);
        return BD_NVDIMM_NAMESPACE_MODE_UNKNOWN;
    }
}

/**
 * bd_nvdimm_namespace_get_mode_str:
 * @mode: mode to get string representation of
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer none): string representation of @mode or %NULL in case of error
 *
 * Tech category: always available
 */
const gchar* bd_nvdimm_namespace_get_mode_str (BDNVDIMMNamespaceMode mode, GError **error) {
    if (mode <= BD_NVDIMM_NAMESPACE_MODE_UNKNOWN)
        return mode_str[mode];
    else {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_MODE_INVAL,
                     "Invalid mode given: %d", mode);
        return NULL;
    }
}

static struct ndctl_namespace* get_namespace_by_name (const gchar *namespace, struct ndctl_ctx *ctx) {
    struct ndctl_namespace *ndns = NULL;
    struct ndctl_region *region = NULL;
    struct ndctl_bus *bus = NULL;

    ndctl_bus_foreach (ctx, bus) {
        ndctl_region_foreach (bus, region) {
            ndctl_namespace_foreach (region, ndns) {
                if (g_strcmp0 (namespace, ndctl_namespace_get_devname (ndns)) == 0)
                    return ndns;
            }
        }
    }

    return NULL;
}

/**
 * bd_nvdimm_namespace_get_devname:
 * @device: name or path of a block device (e.g. "/dev/pmem0")
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): namespace device name (e.g. "namespaceX.Y") for @device
 *                           or %NULL if @device is not a NVDIMM namespace
 *                           (@error may be set to indicate error)
 *
 * Tech category: %BD_NVDIMM_TECH_NAMESPACE-%BD_NVDIMM_TECH_MODE_QUERY
 */
gchar* bd_nvdimm_namespace_get_devname (const gchar *device, GError **error) {
    struct ndctl_ctx *ctx = NULL;
    struct ndctl_namespace *ndns = NULL;
    struct ndctl_region *region = NULL;
    struct ndctl_bus *bus = NULL;
    gint success = 0;
    gchar *ret = NULL;

    /* get rid of the "/dev/" prefix (if any) */
    if (g_str_has_prefix (device, "/dev/"))
        device = device + 5;

    success = ndctl_new (&ctx);
    if (success != 0) {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                     "Failed to create ndctl context");
        return FALSE;
    }

    ndctl_bus_foreach (ctx, bus) {
        ndctl_region_foreach (bus, region) {
            ndctl_namespace_foreach (region, ndns) {
                if (!ndctl_namespace_is_active (ndns))
                    continue;

                struct ndctl_btt *btt = ndctl_namespace_get_btt (ndns);
                struct ndctl_dax *dax = ndctl_namespace_get_dax (ndns);
                struct ndctl_pfn *pfn = ndctl_namespace_get_pfn (ndns);
                const gchar *blockdev = NULL;

                if (dax)
                    continue;
                else if (btt)
                    blockdev = ndctl_btt_get_block_device (btt);
                else if (pfn)
                    blockdev = ndctl_pfn_get_block_device (pfn);
                else
                    blockdev = ndctl_namespace_get_block_device (ndns);

                if (g_strcmp0 (blockdev, device) == 0) {
                    ret = g_strdup (ndctl_namespace_get_devname (ndns));
                    ndctl_unref (ctx);
                    return ret;
                }
            }
        }
    }

    ndctl_unref (ctx);
    return NULL;
}

/**
 * bd_nvdimm_namespace_enable:
 * @namespace: name of the namespace to enable
 * @extra: (allow-none) (array zero-terminated=1): extra options (currently unused)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @namespace was successfully enabled or not
 *
 * Tech category: %BD_NVDIMM_TECH_NAMESPACE-%BD_NVDIMM_TECH_MODE_ACTIVATE_DEACTIVATE
 */
gboolean bd_nvdimm_namespace_enable (const gchar *namespace, const BDExtraArg **extra UNUSED, GError **error) {
    struct ndctl_ctx *ctx = NULL;
    struct ndctl_namespace *ndns = NULL;
    gint ret = 0;

    ret = ndctl_new (&ctx);
    if (ret != 0) {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                     "Failed to create ndctl context");
        return FALSE;
    }

    ndns = get_namespace_by_name (namespace, ctx);
    if (!ndns) {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_NOEXIST,
                     "Failed to enable namespace: namespace '%s' not found.", namespace);
        return FALSE;
    }

    ret = ndctl_namespace_enable (ndns);
    if (ret < 0) {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                     "Failed to enable namespace: %s", strerror (-ret));
        ndctl_unref (ctx);
        return FALSE;
    }

    ndctl_unref (ctx);
    return TRUE;
}

/**
 * bd_nvdimm_namespace_disable:
 * @namespace: name of the namespace to disable
 * @extra: (allow-none) (array zero-terminated=1): extra options (currently unused)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @namespace was successfully disabled or not
 *
 * Tech category: %BD_NVDIMM_TECH_NAMESPACE-%BD_NVDIMM_TECH_MODE_ACTIVATE_DEACTIVATE
 */
gboolean bd_nvdimm_namespace_disable (const gchar *namespace, const BDExtraArg **extra UNUSED, GError **error) {
    struct ndctl_ctx *ctx = NULL;
    struct ndctl_namespace *ndns = NULL;
    gint ret = 0;

    ret = ndctl_new (&ctx);
    if (ret != 0) {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                     "Failed to create ndctl context");
        return FALSE;
    }

    ndns = get_namespace_by_name (namespace, ctx);
    if (!ndns) {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_NOEXIST,
                     "Failed to disable namespace: namespace '%s' not found.", namespace);
        return FALSE;
    }

    ret = ndctl_namespace_disable_safe (ndns);
    if (ret != 0) {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                     "Failed to disable namespace: %s", strerror (-ret));
        ndctl_unref (ctx);
        return FALSE;
    }

    ndctl_unref (ctx);
    return TRUE;
}

static BDNVDIMMNamespaceInfo* get_nvdimm_namespace_info (struct ndctl_namespace *ndns, GError **error) {
    struct ndctl_btt *btt;
    struct ndctl_pfn *pfn;
    struct ndctl_dax *dax;
    enum ndctl_namespace_mode mode;
    gchar uuid_buf[37] = {0};
    uuid_t uuid;

    btt = ndctl_namespace_get_btt (ndns);
    dax = ndctl_namespace_get_dax (ndns);
    pfn = ndctl_namespace_get_pfn (ndns);
    mode = ndctl_namespace_get_mode (ndns);

    BDNVDIMMNamespaceInfo *info = g_new0 (BDNVDIMMNamespaceInfo, 1);

    info->dev = g_strdup (ndctl_namespace_get_devname (ndns));

    switch (mode) {
        case NDCTL_NS_MODE_MEMORY:
            if (pfn)
              info->size = ndctl_pfn_get_size (pfn);
            else
              info->size = ndctl_namespace_get_size (ndns);
#ifndef LIBNDCTL_NEW_MODES
          info->mode = BD_NVDIMM_NAMESPACE_MODE_MEMORY;
#else
          info->mode = BD_NVDIMM_NAMESPACE_MODE_FSDAX;
#endif
          break;
        case NDCTL_NS_MODE_DAX:
            if (!dax) {
                g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                             "Failed to get information about namespaces: DAX mode "
                             "detected but no DAX device found.");
                bd_nvdimm_namespace_info_free (info);
                return NULL;
            }
            info->size = ndctl_dax_get_size (dax);
#ifndef LIBNDCTL_NEW_MODES
            info->mode = BD_NVDIMM_NAMESPACE_MODE_DAX;
#else
            info->mode = BD_NVDIMM_NAMESPACE_MODE_DEVDAX;
#endif
            break;
        case NDCTL_NS_MODE_SAFE:
            if (!btt) {
                g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                             "Failed to get information about namespaces: Sector mode "
                             "detected but no BTT device found.");
                bd_nvdimm_namespace_info_free (info);
                return NULL;
            }
            info->size = ndctl_btt_get_size (btt);
            info->mode = BD_NVDIMM_NAMESPACE_MODE_SECTOR;
            break;
        case NDCTL_NS_MODE_RAW:
            info->size = ndctl_namespace_get_size (ndns);
            info->mode = BD_NVDIMM_NAMESPACE_MODE_RAW;
            break;
        default:
            g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                         "Failed to get information about namespaces: Unknow mode.");
            bd_nvdimm_namespace_info_free (info);
            return NULL;
    }

    if (btt) {
        ndctl_btt_get_uuid (btt, uuid);
        uuid_unparse (uuid, uuid_buf);
        info->uuid = g_strdup (uuid_buf);

        info->blockdev = g_strdup (ndctl_btt_get_block_device (btt));
    } else if (pfn) {
        ndctl_pfn_get_uuid (pfn, uuid);
        uuid_unparse (uuid, uuid_buf);
        info->uuid = g_strdup (uuid_buf);

        info->blockdev = g_strdup (ndctl_pfn_get_block_device (pfn));
    } else if (dax) {
        ndctl_dax_get_uuid (dax, uuid);
        uuid_unparse (uuid, uuid_buf);
        info->uuid = g_strdup (uuid_buf);

        /* no blockdev for dax mode */
        info->blockdev = NULL;
    } else {
        ndctl_namespace_get_uuid (ndns, uuid);

        if (uuid_is_null (uuid))
            info->uuid = NULL;
        else {
            uuid_unparse (uuid, uuid_buf);
            info->uuid = g_strdup (uuid_buf);
        }

        info->blockdev = g_strdup (ndctl_namespace_get_block_device (ndns));
    }

    if (btt)
        info->sector_size = ndctl_btt_get_sector_size (btt);
    else if (dax)
        /* no sector size for dax mode */
        info->sector_size = 0;
    else {
        info->sector_size = ndctl_namespace_get_sector_size (ndns);

        /* apparently the default value for sector size is 512
           on non DAX namespaces even if libndctl says it's 0
           https://github.com/pmem/ndctl/commit/a7320456f1bca5edf15352ce977e757fdf78ed58
         */

        if (info->sector_size == 0)
            info->sector_size = 512;
    }

    info->enabled = ndctl_namespace_is_active (ndns);

    return info;
}

/**
 * bd_nvdimm_namespace_info:
 * @namespace: namespace to get information about
 * @extra: (allow-none) (array zero-terminated=1): extra options (currently unused)
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about given namespace or %NULL if no such
 *                           namespace was found (@error may be set to indicate error)
 *
 * Tech category: %BD_NVDIMM_TECH_NAMESPACE-%BD_NVDIMM_TECH_MODE_QUERY
 */
BDNVDIMMNamespaceInfo* bd_nvdimm_namespace_info (const gchar *namespace, const BDExtraArg **extra UNUSED, GError **error) {
    struct ndctl_ctx *ctx = NULL;
    struct ndctl_namespace *ndns = NULL;
    BDNVDIMMNamespaceInfo *info = NULL;
    gint ret = 0;

    ret = ndctl_new (&ctx);
    if (ret != 0) {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                     "Failed to create ndctl context");
        return NULL;
    }

    ndns = get_namespace_by_name (namespace, ctx);
    if (ndns) {
        info = get_nvdimm_namespace_info (ndns, error);
        ndctl_unref (ctx);
        return info;
    }

    ndctl_unref (ctx);
    return NULL;
}

/**
 * bd_nvdimm_list_namespaces:
 * @bus_name: (allow-none): return only namespaces on given bus (specified by name),
 *                          %NULL may be specified to return namespaces from all buses
 * @region_name: (allow-none): return only namespaces on given region (specified by 'regionX' name),
 *                             %NULL may be specified to return namespaces from all regions
 * @idle: whether to list idle (not enabled) namespaces too
 * @extra: (allow-none) (array zero-terminated=1): extra options (currently unused)
 * @error: (out): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about the namespaces on @bus and @region or
 *                                     %NULL if no namespaces were found (@error may be set to indicate error)
 *
 * Tech category: %BD_NVDIMM_TECH_NAMESPACE-%BD_NVDIMM_TECH_MODE_QUERY
 */
BDNVDIMMNamespaceInfo** bd_nvdimm_list_namespaces (const gchar *bus_name, const gchar *region_name, gboolean idle, const BDExtraArg **extra UNUSED, GError **error) {
    struct ndctl_ctx *ctx = NULL;
    struct ndctl_namespace *ndns = NULL;
    struct ndctl_region *region = NULL;
    struct ndctl_bus *bus = NULL;
    gint ret = 0;
    BDNVDIMMNamespaceInfo **info = NULL;

    GPtrArray *namespaces = g_ptr_array_new ();

    ret = ndctl_new (&ctx);
    if (ret != 0) {
        g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_FAIL,
                     "Failed to create ndctl context");
        return NULL;
    }

    ndctl_bus_foreach (ctx, bus) {
        if (bus_name && g_strcmp0 (bus_name, ndctl_bus_get_devname (bus)) != 0)
            continue;

        ndctl_region_foreach (bus, region) {
            if (region_name && g_strcmp0 (bus_name, ndctl_region_get_devname (region)) != 0)
                continue;

            ndctl_namespace_foreach (region, ndns) {
                if (!idle && !ndctl_namespace_is_active (ndns))
                    continue;

                BDNVDIMMNamespaceInfo *info = get_nvdimm_namespace_info (ndns, error);
                if (!info) {
                    g_ptr_array_foreach (namespaces, (GFunc) (void *) bd_nvdimm_namespace_info_free, NULL);
                    g_ptr_array_free (namespaces, FALSE);
                    ndctl_unref (ctx);
                    return NULL;
                }

                g_ptr_array_add (namespaces, info);
            }
        }
    }

    if (namespaces->len == 0) {
        ndctl_unref (ctx);
        return NULL;
    }

    g_ptr_array_add (namespaces, NULL);

    info = (BDNVDIMMNamespaceInfo **) g_ptr_array_free (namespaces, FALSE);
    ndctl_unref (ctx);

    return info;
}

/**
 * bd_nvdimm_namespace_reconfigure:
 * @namespace: name of the namespace to recofigure
 * @mode: mode type to set
 * @error: (out): place to store error if any
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'ndctl' utility)
 *
 * Returns: whether @namespace was successfully reconfigured or not
 *
 * Tech category: %BD_NVDIMM_TECH_NAMESPACE-%BD_NVDIMM_TECH_MODE_RECONFIGURE
 */
gboolean bd_nvdimm_namespace_reconfigure (const gchar* namespace, BDNVDIMMNamespaceMode mode, gboolean force, const BDExtraArg **extra, GError** error) {
    const gchar *args[8] = {"ndctl", "create-namespace", "-e", namespace, "-m", NULL, NULL, NULL};
    gboolean ret = FALSE;
    const gchar *mode_str = NULL;

    if (!check_deps (&avail_deps, DEPS_NDCTL_MASK, deps, DEPS_LAST, &deps_check_lock, error))
      return FALSE;

    mode_str = bd_nvdimm_namespace_get_mode_str (mode, error);
    if (!mode_str)
        /* error is already populated */
        return FALSE;

    args[5] = g_strdup (mode_str);

    if (force)
      args[6] = "-f";

    ret = bd_utils_exec_and_report_error (args, extra, error);

    g_free ((gchar *) args[5]);
    return ret;
}


static guint64 blk_sector_sizes[] = { 512, 520, 528, 4096, 4104, 4160, 4224, 0 };
static guint64 pmem_sector_sizes[] = { 512, 4096, 0 };
static guint64 io_sector_sizes[] = { 0 };

/**
 * bd_nvdimm_namepace_get_supported_sector_sizes:
 * @mode: namespace mode
 * @error: (out): place to store error if any
 *
 * Returns: (transfer none) (array zero-terminated=1): list of supported sector sizes for @mode
 *
 * Tech category: %BD_NVDIMM_TECH_NAMESPACE-%BD_NVDIMM_TECH_MODE_QUERY
 */
const guint64 *bd_nvdimm_namepace_get_supported_sector_sizes (BDNVDIMMNamespaceMode mode, GError **error) {
    switch (mode) {
        case BD_NVDIMM_NAMESPACE_MODE_RAW:
        case BD_NVDIMM_NAMESPACE_MODE_MEMORY:
        case BD_NVDIMM_NAMESPACE_MODE_FSDAX:
            return pmem_sector_sizes;

        case BD_NVDIMM_NAMESPACE_MODE_DAX:
        case BD_NVDIMM_NAMESPACE_MODE_DEVDAX:
            return io_sector_sizes;

        case BD_NVDIMM_NAMESPACE_MODE_SECTOR:
            return blk_sector_sizes;

        default:
            g_set_error (error, BD_NVDIMM_ERROR, BD_NVDIMM_ERROR_NAMESPACE_MODE_INVAL,
                         "Invalid/unknown mode specified.");
            return NULL;
    }
}
