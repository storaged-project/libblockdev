/*
 * Copyright (C) 2014-2018  Red Hat, Inc.
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
 *         Gris Ge <fge@redhat.com>
 */

/*
 * Notes on Code style:
 *  * 4 spaces instead of tab as indention.
 *  * Use this format of function block:
 *      const char *something (int arg1) {
 *      }
 *  * Space before '(' unless it's in macro define.
 *  * Use this format for if/switch/for block:
 *      if (foo != boo) {
 *          do_something ();
 *      }
 */

#include <glib.h>
#include <unistd.h>
#include <blockdev/utils.h>
#include <libdmmp/libdmmp.h>
#include <string.h>
#include <stdio.h>

#include "mpath.h"
#include "check_deps.h"

#define _ERR_MSG_BUFF_SIZE    4096

/*
 * Just don't print to stdout or stderr, and save last error message
 */
static void _log_func (struct dmmp_context *ctx, int priority,
                       const char *file, int line, const char *func_name,
                       const char *format, va_list args);

static const char *_dmmp_last_err_msg (struct dmmp_context *ctx);

#define _dmmp_ctx_setup(ctx, err_msg_buff) \
    do { \
        ctx = dmmp_context_new (); \
        if (ctx != NULL) \
          { \
            memset (err_msg_buff, 0, _ERR_MSG_BUFF_SIZE); \
            dmmp_context_userdata_set (ctx, err_msg_buff); \
            dmmp_context_log_func_set (ctx, _log_func); \
          } \
    } while (0)

#define _UNUSED(x) (void)(x)

/**
 * SECTION: mpath
 * @short_description: plugin for basic operations with multipath devices
 * @title: Mpath
 * @include: mpath.h
 *
 * A plugin for basic operations with multipath devices.
 */

/**
 * bd_mpath_error_quark: (skip)
 */
GQuark bd_mpath_error_quark (void) {
    return g_quark_from_static_string ("g-bd-mpath-error-quark");
}

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MPATHCONF 0
#define DEPS_MPATHCONF_MASK (1 << DEPS_MPATHCONF)
#define DEPS_LAST 1

static UtilDep deps[DEPS_LAST] = {
    {"mpathconf", NULL, NULL, NULL},
};

/**
 * bd_mpath_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_mpath_check_deps () {
    GError *error = NULL;
    guint i = 0;
    gboolean status = FALSE;
    gboolean ret = TRUE;

    for (i=0; i < DEPS_LAST; i++) {
        status = bd_utils_check_util_version (deps[i].name, deps[i].version,
                                              deps[i].ver_arg,
                                              deps[i].ver_regexp, &error);
        if (!status)
            g_warning ("%s", error->message);
        else
            g_atomic_int_or (&avail_deps, 1 << i);
        g_clear_error (&error);
        ret = ret && status;
    }

    if (!ret)
        g_warning ("Cannot load the multipath plugin");

    return ret;
}

/**
 * bd_mpath_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_mpath_init () {
    /* nothing to do here */
    return TRUE;
};

/**
 * bd_mpath_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_mpath_close () {
    /* nothing to do here */
}

/**
 * bd_mpath_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_mpath_is_tech_avail (BDMpathTech tech, guint64 mode, GError **error) {
    switch (tech) {
    case BD_MPATH_TECH_BASE:
        return TRUE;
    case BD_MPATH_TECH_FRIENDLY_NAMES:
        if (mode & ~BD_MPATH_TECH_MODE_MODIFY) {
            g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_TECH_UNAVAIL,
                         "Only 'modify' (setting) supported for friendly names");
            return FALSE;
        } else if (mode & BD_MPATH_TECH_MODE_MODIFY)
            return check_deps (&avail_deps, DEPS_MPATHCONF_MASK, deps, DEPS_LAST, &deps_check_lock, error);
        else {
            g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_TECH_UNAVAIL,
                         "Unknown mode");
            return FALSE;
        }
    default:
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_TECH_UNAVAIL, "Unknown technology");
        return FALSE;
    }
}


/**
 * bd_mpath_flush_mpaths:
 * @error: (out): place to store error (if any)
 *
 * Returns: whether multipath device maps were successfully flushed or not
 *
 * Flushes all unused multipath device maps.
 *
 * Tech category: %BD_MPATH_TECH_BASE-%BD_MPATH_TECH_MODE_MODIFY
 */
gboolean bd_mpath_flush_mpaths (GError **error) {
    struct dmmp_mpath **mps = NULL;
    struct dmmp_context *ctx = NULL;
    uint32_t mp_count = 0;
    uint32_t i = 0;
    const char *name = NULL;
    int dmmp_rc = DMMP_OK;
    char err_msg_buff[_ERR_MSG_BUFF_SIZE];

    if (error == NULL)
        return FALSE;

    *error = NULL;

    if (geteuid () != 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NOT_ROOT,
                     "Not running as root, cannot flush mpaths");
        return FALSE;
    }

    _dmmp_ctx_setup (ctx, err_msg_buff);
    if (ctx == NULL) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                     "No enough memory to create dmmp context");
        return FALSE;
    }

    dmmp_rc = dmmp_mpath_array_get (ctx, &mps, &mp_count);

    if (dmmp_rc != DMMP_OK) {
        dmmp_context_free (ctx);
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                     "Failed to query existing multipath devices, error %d: "
                     "%s", dmmp_rc, _dmmp_last_err_msg (ctx));
        return FALSE;
    }

    if (mp_count == 0) {
        dmmp_mpath_array_free (mps, mp_count);
        dmmp_context_free (ctx);
        return TRUE;
    }

    for (i = 0; i < mp_count; ++i) {
        name = dmmp_mpath_name_get (mps[i]);
        dmmp_flush_mpath (ctx, name);
    }

    dmmp_mpath_array_free (mps, mp_count);
    dmmp_rc = dmmp_mpath_array_get (ctx, &mps, &mp_count);

    if (dmmp_rc != DMMP_OK) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                     "Failed to check remaining multipath devices after flush: "
                     "error %d: %s", dmmp_rc, _dmmp_last_err_msg (ctx));
        dmmp_context_free (ctx);
        return FALSE;
    }


    dmmp_mpath_array_free (mps, mp_count);
    dmmp_context_free (ctx);
    if (mp_count == 0)
        return TRUE;
    else
        return FALSE;
}

/**
 * bd_mpath_reconfig:
 * @error: (out): place to store error (if any)
 *
 * Returns: whether multipath device maps were successfully reconfigured.
 *
 * Ask the multipathd to reload configuration(e.g. /etc/multipath.conf) and add
 * missing mpath back.
 *
 * Tech category: %BD_MPATH_TECH_BASE-%BD_MPATH_TECH_MODE_MODIFY
 */
gboolean bd_mpath_reconfig (GError **error) {
    struct dmmp_context *ctx = NULL;
    int dmmp_rc = DMMP_OK;
    gboolean rc = FALSE;
    char err_msg_buff[_ERR_MSG_BUFF_SIZE];

    if (error == NULL)
        return FALSE;

    *error = NULL;

    if (geteuid () != 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NOT_ROOT,
                     "Not running as root, cannot flush mpaths");
        return FALSE;
    }

    _dmmp_ctx_setup (ctx, err_msg_buff);
    if (ctx == NULL) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                     "No enough memory to create dmmp context");
        return FALSE;
    }

    dmmp_rc = dmmp_reconfig (ctx);

    if (dmmp_rc != DMMP_OK)
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_RECONFIG,
                     "Failed to reload multipath configuration, error %d: %s",
                     dmmp_rc, _dmmp_last_err_msg (ctx));
    else
        rc = TRUE;

    dmmp_context_free (ctx);
    return rc;
}

/**
 * bd_mpath_is_mpath_member:
 * @device: device to test
 * @error: (out): place to store error (if any)
 *
 * Returns: %TRUE if the device is a multipath member, %FALSE if not or an
 * error appeared when queried (@error is set in those cases)
 *
 * Tech category: %BD_MPATH_TECH_BASE-%BD_MPATH_TECH_MODE_QUERY
 */
gboolean bd_mpath_is_mpath_member (const gchar *device, GError **error) {
    gchar *dev_path = NULL;
    gboolean rc = FALSE;
    struct dmmp_context *ctx = NULL;
    struct dmmp_mpath **mps = NULL;
    uint32_t mp_count = 0;
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t l = 0;
    struct dmmp_path_group **pgs = NULL;
    uint32_t pg_count = 0;
    struct dmmp_path **ps = NULL;
    uint32_t p_count = 0;
    const char *blk_name = NULL;
    gchar *dev_name = NULL;
    int dmmp_rc = DMMP_OK;
    char err_msg_buff[_ERR_MSG_BUFF_SIZE];

    if (error == NULL)
        return FALSE;

    *error = NULL;

    if (device == NULL) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_INVAL,
                     "Invalid argument: device pointer is NULL");
        return FALSE;
    }

    /* in case the device is dev_path, we need to resolve it because maps's deps
       are devices and not their dev_paths */
    if (g_str_has_prefix (device, "/dev/mapper/") ||
        g_str_has_prefix (device, "/dev/md/")) {
        dev_path = bd_utils_resolve_device (device, error);
        if (dev_path == NULL) {
            /* the device doesn't exist and thus is not an mpath member */
            return FALSE;
        }
        dev_name = g_path_get_basename (dev_path);
        if (dev_name == NULL) {
            g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_INVAL,
                         "Invalid argument: failed to find basename of device "
                         "'%s'", dev_path);
            g_free (dev_path);
            return FALSE;
        }
    } else {
        dev_name = g_path_get_basename (device);
        if (dev_name == NULL) {
            g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_INVAL,
                         "Invalid argument: failed to find basename of device "
                         "'%s'", device);
            return FALSE;
        }
    }

    _dmmp_ctx_setup (ctx, err_msg_buff);
    if (ctx == NULL) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                     "No enough memory to create dmmp context");
        g_free (dev_path);
        g_free (dev_name);
        return FALSE;
    }
    dmmp_rc = dmmp_mpath_array_get (ctx, &mps, &mp_count);

    if (dmmp_rc != DMMP_OK) {
        g_free (dev_path);
        g_free (dev_name);
        dmmp_context_free (ctx);
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                     "Failed to query existing multipath devices, error %d: "
                     "%s", dmmp_rc, _dmmp_last_err_msg (ctx));
        return FALSE;
    }

    if (mp_count == 0) {
        g_free (dev_path);
        g_free (dev_name);
        dmmp_context_free (ctx);
        return FALSE;
    }

    for (i = 0; i < mp_count; ++i) {
        dmmp_path_group_array_get (mps[i], &pgs, &pg_count);
        for (j = 0; j < pg_count; ++j) {
            dmmp_path_array_get (pgs[j], &ps, &p_count);
            for (l = 0; l < p_count; ++l) {
                blk_name = dmmp_path_blk_name_get (ps[l]);
                if (g_strcmp0 (blk_name, dev_name) == 0) {
                    rc = TRUE;
                    break;
                }
            }
        }
    }

    dmmp_mpath_array_free (mps, mp_count);
    dmmp_context_free (ctx);
    g_free (dev_path);
    g_free (dev_name);
    return rc;
}

/**
 * bd_mpath_get_mpath_members:
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): list of names of all
 * devices that are members of the mpath mappings (or %NULL in case of error)
 *
 * Tech category: %BD_MPATH_TECH_BASE-%BD_MPATH_TECH_MODE_QUERY
 */
gchar** bd_mpath_get_mpath_members (GError **error) {
    struct dmmp_context *ctx = NULL;
    struct dmmp_mpath **mps = NULL;
    uint32_t mp_count = 0;
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t l = 0;
    struct dmmp_path_group **pgs = NULL;
    uint32_t pg_count = 0;
    struct dmmp_path **ps = NULL;
    uint32_t p_count = 0;
    int dmmp_rc = DMMP_OK;
    const char *blk_name = NULL;
    gchar **ret = NULL;
    GList *path_blk_names = NULL;
    gchar *dup = NULL;
    guint len = 0;
    // ^ Use this counter to eliminate duplicate iterate of g_list_length ()
    guint m = 0;
    GList *cur_node = NULL;
    char err_msg_buff[_ERR_MSG_BUFF_SIZE];

    if (error == NULL)
        return NULL;

    *error = NULL;

    _dmmp_ctx_setup (ctx, err_msg_buff);
    if (ctx == NULL) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                     "No enough memory to create dmmp context");
        return NULL;
    }
    dmmp_rc = dmmp_mpath_array_get (ctx, &mps, &mp_count);

    if (dmmp_rc != DMMP_OK) {
        dmmp_context_free (ctx);
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                     "Failed to query existing multipath devices, error %d: "
                     "%s", dmmp_rc, _dmmp_last_err_msg (ctx));
        return NULL;
    }

    if (mp_count == 0) {
        ret = g_new0(gchar*, 1);
        if (ret == NULL)
            g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                         "No enough memory to create returned string list");
        dmmp_context_free (ctx);
        return NULL;
    }

    for (i = 0; i < mp_count; ++i) {
        dmmp_path_group_array_get (mps[i], &pgs, &pg_count);
        for (j = 0; j < pg_count; ++j) {
            dmmp_path_array_get (pgs[j], &ps, &p_count);
            for (l = 0; l < p_count; ++l) {
                blk_name = dmmp_path_blk_name_get (ps[l]);
                if (blk_name == NULL)
                    continue;
                dup = g_strdup (blk_name);
                if (dup == NULL) {
                    g_set_error (error, BD_MPATH_ERROR,
                                 BD_MPATH_ERROR_NO_MEMORY,
                                 "No enough memory to duplicate mpath member "
                                 "device path string");

                    dmmp_mpath_array_free (mps, mp_count);
                    dmmp_context_free (ctx);
                    if (path_blk_names != NULL)
                        g_list_free_full (path_blk_names, g_free);
                    return NULL;
                }
                path_blk_names = g_list_prepend (path_blk_names,
                                                 (gpointer) dup);
                if (path_blk_names == NULL) {
                    g_set_error (error, BD_MPATH_ERROR,
                                 BD_MPATH_ERROR_NO_MEMORY,
                                 "No enough memory to append mpath member "
                                 "device path string to list");

                    dmmp_mpath_array_free (mps, mp_count);
                    dmmp_context_free (ctx);
                    return NULL;
                }
                len++;
            }
        }
    }

    dmmp_mpath_array_free (mps, mp_count);
    dmmp_context_free (ctx);
    mps = NULL;
    ctx = NULL;
    // ^ Eliminate the dangling pointers.
    mp_count = 0;

    ret = g_new0(gchar*, len + 1);
    if (ret == NULL) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NO_MEMORY,
                     "No enough memory to create dmmp context");
        if (path_blk_names != NULL)
            g_list_free_full (path_blk_names, g_free);
        return NULL;
    }
    if (len == 0)
        // This might happen when path groups are not removed while all paths
        // are removed
        return ret;

    cur_node = g_list_first (path_blk_names);
    if (cur_node == NULL) {
        // Unexpected, just make sure we return error instead of crash on
        // dereference on NULL pointer: cur_node->data.
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_GET_MEMBERS,
                     "BUG of libblock mpath plugin: got NULL return from "
                     "g_list_first () on path_blk_names: %p", path_blk_names);
        g_free (ret);
        return NULL;
    }

    ret[0] = cur_node->data;
    while (cur_node != NULL) {
      cur_node = g_list_next (cur_node);
      if (cur_node == NULL)
        continue;
      if (m + 1 >= len)
        break;
      ret[m + 1] = (char *) cur_node->data;
      m++;
    }

    g_list_free (path_blk_names);

    return ret;
}


/**
 * bd_mpath_set_friendly_names:
 * @enabled: whether friendly names should be enabled or not
 * @error: (out): place to store error (if any)
 *
 * Returns: if successfully set or not
 *
 * Tech category: %BD_MPATH_TECH_FRIENDLY_NAMES-%BD_MPATH_TECH_MODE_MODIFY
 */
gboolean bd_mpath_set_friendly_names (gboolean enabled, GError **error) {
      const gchar *argv[8] = {"mpathconf", "--find_multipaths", "y",
                              "--user_friendly_names", NULL,
                              "--with_multipathd", "y", NULL};
      argv[4] = enabled ? "y" : "n";

    if (!check_deps (&avail_deps, DEPS_MPATHCONF_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, NULL, error);
}

static void _log_func (struct dmmp_context *ctx, int priority, const char *file,
                       int line, const char *func_name, const char *format,
                       va_list args) {
  char *err_msg_buff = NULL;

  _UNUSED(file);
  _UNUSED(line);
  _UNUSED(func_name);

  err_msg_buff = dmmp_context_userdata_get (ctx);
  if (err_msg_buff == NULL)
    return;

  if (priority == DMMP_LOG_PRIORITY_ERROR)
    vsnprintf (err_msg_buff, _ERR_MSG_BUFF_SIZE, format, args);
}

static const char *_dmmp_last_err_msg (struct dmmp_context *ctx) {
  char *err_msg_buff = NULL;

  err_msg_buff = dmmp_context_userdata_get (ctx);
  return err_msg_buff;
}
