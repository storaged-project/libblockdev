/*
 * Copyright (C) 2017  Red Hat, Inc.
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

#define _GNU_SOURCE
#include <unistd.h>

#include <glib.h>

#include <libmount/libmount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "fs.h"
#include "mount.h"

#define MOUNT_ERR_BUF_SIZE 1024

typedef struct MountArgs {
    const gchar *mountpoint;
    const gchar *device;
    const gchar *fstype;
    const gchar *options;
    const gchar *spec;
    gboolean lazy;
    gboolean force;
} MountArgs;

typedef gboolean (*MountFunc) (MountArgs *args, GError **error);

static gboolean do_mount (MountArgs *args, GError **error);

#ifndef LIBMOUNT_NEW_ERR_API
static gboolean get_unmount_error_old (struct libmnt_context *cxt, int rc, const gchar *spec, GError **error) {
    int syscall_errno = 0;
    int helper_status = 0;

    if (mnt_context_syscall_called (cxt) == 1) {
        syscall_errno = mnt_context_get_syscall_errno (cxt);
        switch (syscall_errno) {
            case 0:
                return TRUE;
            case EBUSY:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Target busy.");
                break;
            case EINVAL:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Not a mount point.");
                break;
            case EPERM:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                             "Operation not permitted.");
                break;
            default:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unmount syscall failed: %d.", syscall_errno);
                break;
        }
    } else if (mnt_context_helper_executed (cxt) == 1) {
        helper_status = mnt_context_get_helper_status (cxt);
        if (helper_status != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Unmount helper program failed: %d.", helper_status);
            return FALSE;
        } else
            return TRUE;
    } else {
        if (rc == 0)
            return TRUE;
        else if (rc == -EPERM) {
            if (mnt_context_tab_applied (cxt))
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                             "Operation not permitted.");
            else
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Not mounted.");
        } else {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to unmount %s.", spec);
        }
    }
    return FALSE;
}
#else
static gboolean get_unmount_error_new (struct libmnt_context *cxt, int rc, const gchar *spec, GError **error) {
    int ret = 0;
    int syscall_errno = 0;
    char buf[MOUNT_ERR_BUF_SIZE] = {0};
    gboolean permission = FALSE;

    ret = mnt_context_get_excode (cxt, rc, buf, MOUNT_ERR_BUF_SIZE - 1);
    if (ret != 0) {
        /* check whether the call failed because of lack of permission */
        if (mnt_context_syscall_called (cxt)) {
            syscall_errno = mnt_context_get_syscall_errno (cxt);
            permission = syscall_errno == EPERM;
        } else
            permission = ret == MNT_EX_USAGE && mnt_context_tab_applied (cxt);

        if (permission)
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                         "Operation not permitted.");
        else {
            if (*buf == '\0')
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unknown error when unmounting %s", spec);
            else
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s", buf);
        }

        return FALSE;
    }

    return TRUE;
}
#endif

static gboolean do_unmount (MountArgs *args, GError **error) {
    struct libmnt_context *cxt = NULL;
    int ret = 0;
    gboolean success = FALSE;

    cxt = mnt_new_context ();

    if (mnt_context_set_target (cxt, args->spec) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to set '%s' as target for umount", args->spec);
        mnt_free_context(cxt);
        return FALSE;
    }

    if (args->lazy) {
        if (mnt_context_enable_lazy (cxt, TRUE) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set lazy unmount for '%s'", args->spec);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    if (args->force) {
        if (mnt_context_enable_force (cxt, TRUE) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set force unmount for '%s'", args->spec);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    ret = mnt_context_umount (cxt);
#ifdef LIBMOUNT_NEW_ERR_API
    success = get_unmount_error_new (cxt, ret, args->spec, error);
#else
    success = get_unmount_error_old (cxt, ret, args->spec, error);
#endif

    mnt_free_context(cxt);
    return success;
}

#ifndef LIBMOUNT_NEW_ERR_API
static gboolean get_mount_error_old (struct libmnt_context *cxt, int rc, MountArgs *args, GError **error) {
    int syscall_errno = 0;
    int helper_status = 0;
    unsigned long mflags = 0;

    if (mnt_context_get_mflags (cxt, &mflags) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get options from string '%s'.", args->options);
        return FALSE;
    }

    if (mnt_context_syscall_called (cxt) == 1) {
        syscall_errno = mnt_context_get_syscall_errno (cxt);
        switch (syscall_errno) {
            case 0:
                return TRUE;
            case EBUSY:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Source is already mounted or target is busy.");
                break;
            case EINVAL:
                if (mflags & MS_REMOUNT)
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Remount attempted, but %s is not mounted at %s.", args->device, args->mountpoint);
                else if (mflags & MS_MOVE)
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Move attempted, but %s is not a mount point.", args->device);
                else
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Wrong fs type, %s has an invalid superblock or missing helper program.", args->device);
                break;
            case EPERM:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                             "Operation not permitted.");
                break;
            case ENOTBLK:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s is not a block device.", args->device);
                break;
            case ENOTDIR:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s is not a directory.", args->mountpoint);
                break;
            case ENODEV:
                if (args->fstype == NULL || strlen (args->fstype) == 0)
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Filesystem type not specified");
                else
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Filesystem type %s not configured in kernel.", args->fstype);
                break;
            case EROFS:
            case EACCES:
                  if (mflags & MS_RDONLY) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "Cannot mount %s read-only.", args->device);
                      break;
                  } else if (args->options && (mnt_optstr_get_option (args->options, "rw", NULL, NULL) == 0)) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "%s is write-protected but `rw' option given.", args->device);
                      break;
                  } else if (mflags & MS_BIND) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "Mount %s on %s failed.", args->device, args->mountpoint);
                      break;
                  }
                  /* new versions of libmount do this automatically */
                  else {
                      MountArgs ro_args;
                      gboolean success = FALSE;

                      ro_args.device = args->device;
                      ro_args.mountpoint = args->mountpoint;
                      ro_args.fstype = args->fstype;
                      if (!args->options)
                          ro_args.options = g_strdup ("ro");
                      else
                          ro_args.options = g_strdup_printf ("%s,ro", args->options);

                      success = do_mount (&ro_args, error);

                      g_free ((gchar*) ro_args.options);

                      return success;
                  }
            default:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Mount syscall failed: %d.", syscall_errno);
                break;
        }
    } else if (mnt_context_helper_executed (cxt) == 1) {
        helper_status = mnt_context_get_helper_status (cxt);
        if (helper_status != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Mount helper program failed: %d.", helper_status);
            return FALSE;
        } else
            return TRUE;
    } else {
        switch (rc) {
            case 0:
                return TRUE;
            case -EPERM:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                             "Only root can mount %s.", args->device);
                break;
            case -EBUSY:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s is already mounted.", args->device);
                break;
            /* source or target explicitly defined and not found in fstab */
            case -MNT_ERR_NOFSTAB:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Can't find %s in %s.", args->device ? args->device : args->mountpoint, mnt_get_fstab_path ());
                break;
            case -MNT_ERR_MOUNTOPT:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to parse mount options");
                break;
            case -MNT_ERR_NOSOURCE:
                if (args->device)
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Can't find %s.", args->device);
                else
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Mount source not defined.");
                break;
            case -MNT_ERR_LOOPDEV:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to setup loop device");
                break;
            case -MNT_ERR_NOFSTYPE:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Filesystem type not specified");
                break;
            default:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to mount %s.", args->device ? args->device : args->mountpoint);
                break;
        }
    }

    return FALSE;
}

#else
static gboolean get_mount_error_new (struct libmnt_context *cxt, int rc, MountArgs *args, GError **error) {
    int ret = 0;
    int syscall_errno = 0;
    char buf[MOUNT_ERR_BUF_SIZE] = {0};
    gboolean permission = FALSE;

    ret = mnt_context_get_excode (cxt, rc, buf, MOUNT_ERR_BUF_SIZE - 1);
    if (ret != 0) {
        /* check whether the call failed because of lack of permission */
        if (mnt_context_syscall_called (cxt) == 1) {
            syscall_errno = mnt_context_get_syscall_errno (cxt);
            permission = syscall_errno == EPERM;
        } else
            permission = ret == MNT_EX_USAGE && mnt_context_tab_applied (cxt);

        if (permission)
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                         "Operation not permitted.");
        else {
            if (*buf == '\0')
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unknown error when mounting %s", args->device ? args->device : args->mountpoint);
            else
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s", buf);
        }

        return FALSE;
    }

    return TRUE;
}
#endif

static gboolean do_mount (MountArgs *args, GError **error) {
    struct libmnt_context *cxt = NULL;
    int ret = 0;
    gboolean success = FALSE;

    cxt = mnt_new_context ();

    if (!args->mountpoint && !args->device) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "You must specify at least one of: mount point, device.");
        mnt_free_context(cxt);
        return FALSE;
    }

    if (args->mountpoint) {
        if (mnt_context_set_target (cxt, args->mountpoint) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set '%s' as target for mount", args->mountpoint);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    if (args->device) {
        if (mnt_context_set_source (cxt, args->device) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set '%s' as source for mount", args->device);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    if (args->fstype) {
        if (mnt_context_set_fstype (cxt, args->fstype) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set '%s' as fstype for mount", args->fstype);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    if (args->options) {
        if (mnt_context_set_options (cxt, args->options) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set '%s' as options for mount", args->options);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

#ifdef LIBMOUNT_NEW_ERR_API
    /* we don't want libmount to try RDONLY mounts if we were explicitly given the "rw" option */
    if (args->options && (mnt_optstr_get_option (args->options, "rw", NULL, NULL) == 0))
        mnt_context_enable_rwonly_mount (cxt, TRUE);
#endif

    ret = mnt_context_mount (cxt);

    /* we need to always do some libmount magic to check if the mount really
       succeeded -- `mnt_context_mount` can return zero when helper program
       (mount.type) fails
     */
#ifdef LIBMOUNT_NEW_ERR_API
    success = get_mount_error_new (cxt, ret, args, error);
#else
    success = get_mount_error_old (cxt, ret, args, error);
#endif

    mnt_free_context(cxt);
    return success;
}

static gboolean set_uid (uid_t uid, GError **error) {
    if (setresuid (uid, -1, -1) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                    "Error setting uid: %m");
        return FALSE;
    }

    return TRUE;
}

static gboolean set_gid (gid_t gid, GError **error) {
    if (setresgid (gid, -1, -1) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                    "Error setting gid: %m");
        return FALSE;
    }

    return TRUE;
}

static gboolean run_as_user (MountFunc func, MountArgs *args, uid_t run_as_uid, gid_t run_as_gid, GError ** error) {
    uid_t current_uid = -1;
    gid_t current_gid = -1;
    pid_t pid = -1;
    pid_t wpid = -1;
    int pipefd[2];
    int status = 0;
    GIOChannel *channel = NULL;
    GError *local_error = NULL;
    gchar *error_msg = NULL;
    gsize msglen = 0;

    current_uid = getuid ();
    current_gid = getgid ();

    if (pipe(pipefd) == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Error creating pipe.");
        return FALSE;
    }

    pid = fork ();

    if (pid == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Error forking.");
        return FALSE;
    } else if (pid == 0) {
        close (pipefd[0]);

        if (run_as_gid != current_gid) {
            if (!set_gid (run_as_gid, error)) {
                if (write(pipefd[1], (*error)->message, strlen((*error)->message)) < 0)
                    _exit (BD_FS_ERROR_PIPE);
                else
                    _exit ((*error)->code);
            }
        }

        if (run_as_uid != current_uid) {
            if (!set_uid (run_as_uid, error)) {
                if (write(pipefd[1], (*error)->message, strlen((*error)->message)) < 0)
                    _exit (BD_FS_ERROR_PIPE);
                else
                    _exit ((*error)->code);
            }
        }

        if (!func (args, error)) {
            if (write(pipefd[1], (*error)->message, strlen((*error)->message)) < 0)
                _exit (BD_FS_ERROR_PIPE);
            else
                _exit ((*error)->code);
        }

        _exit (EXIT_SUCCESS);

    } else {
        close (pipefd[1]);

        do {
            wpid = waitpid (pid, &status, WUNTRACED | WCONTINUED);
            if (wpid == -1) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Error while waiting for process.");
                close (pipefd[0]);
                return FALSE;
            }

            if (WIFEXITED (status)) {
              if (WEXITSTATUS (status) != EXIT_SUCCESS) {
                  if (WEXITSTATUS (status) == BD_FS_ERROR_PIPE) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "Error while reading error.");
                      close (pipefd[0]);
                      return FALSE;
                  }

                  channel = g_io_channel_unix_new (pipefd[0]);
                  if (g_io_channel_read_to_end (channel, &error_msg, &msglen, &local_error) != G_IO_STATUS_NORMAL) {
                      if (local_error) {
                          g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                       "Error while reading error: %s (%d)",
                                       local_error->message, local_error->code);
                          g_clear_error (&local_error);
                      } else
                          g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                       "Unknoen error while reading error.");
                      g_io_channel_unref (channel);
                      close (pipefd[0]);
                      g_free (error_msg);
                      return FALSE;
                  }

                  if (g_io_channel_shutdown (channel, TRUE, &local_error) == G_IO_STATUS_ERROR) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "Error shutting down GIO channel: %s (%d)",
                                   local_error->message, local_error->code);
                      g_clear_error (&local_error);
                      g_io_channel_unref (channel);
                      close (pipefd[0]);
                      g_free (error_msg);
                      return FALSE;
                  }

                  if (WEXITSTATUS (status) > BD_FS_ERROR_AUTH)
                      g_set_error_literal (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                           error_msg);
                  else
                      g_set_error_literal (error, BD_FS_ERROR, WEXITSTATUS (status),
                                           error_msg);

                  g_io_channel_unref (channel);
                  close (pipefd[0]);
                  g_free (error_msg);
                  return FALSE;
              } else {
                  close (pipefd[0]);
                  return TRUE;
              }
            } else if (WIFSIGNALED (status)) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Killed by signal %d.", WTERMSIG(status));
                close (pipefd[0]);
                return FALSE;
            }

        } while (!WIFEXITED (status) && !WIFSIGNALED (status));
        close (pipefd[0]);
    }

    return FALSE;
}

/**
 * bd_fs_unmount:
 * @spec: mount point or device to unmount
 * @lazy: enable/disable lazy unmount
 * @force: enable/disable force unmount
 * @extra: (allow-none) (array zero-terminated=1): extra options for the unmount
 *                                                 currently only 'run_as_uid'
 *                                                 and 'run_as_gid' are supported
 *                                                 value must be a valid non zero
 *                                                 uid (gid)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether @spec was successfully unmounted or not
 *
 * Tech category: %BD_FS_TECH_GENERIC (no mode, ignored)
 */
gboolean bd_fs_unmount (const gchar *spec, gboolean lazy, gboolean force, const BDExtraArg **extra, GError **error) {
    uid_t run_as_uid = -1;
    gid_t run_as_gid = -1;
    uid_t current_uid = -1;
    gid_t current_gid = -1;
    const BDExtraArg **extra_p = NULL;
    gchar *endptr = NULL;
    MountArgs args;

    args.spec = spec;
    args.lazy = lazy;
    args.force = force;

    current_uid = getuid ();
    run_as_uid = current_uid;

    current_gid = getgid ();
    run_as_gid = current_gid;

    if (extra) {
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "run_as_uid") == 0)) {
                run_as_uid = g_ascii_strtoull ((*extra_p)->val, &endptr, 0);

                /* g_ascii_strtoull returns 0 in case of error */
                if (run_as_uid == 0 && endptr == (*extra_p)->val) {
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Invalid specification of UID: '%s'", (*extra_p)->val);
                    return FALSE;
                }
            } else if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "run_as_gid") == 0)) {
                run_as_gid = g_ascii_strtoull ((*extra_p)->val, &endptr, 0);

                /* g_ascii_strtoull returns 0 in case of error */
                if (run_as_gid == 0 && endptr == (*extra_p)->val) {
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Invalid specification of GID: '%s'", (*extra_p)->val);
                    return FALSE;
                }
            } else {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unsupported argument for unmount: '%s'", (*extra_p)->opt);
                return FALSE;
            }
        }
    }

    if (run_as_uid != current_uid || run_as_gid != current_gid) {
        return run_as_user ((MountFunc) do_unmount, &args, run_as_uid, run_as_gid, error);
    } else
        return do_unmount (&args, error);

    return TRUE;
}

/**
 * bd_fs_mount:
 * @device: (allow-none): device to mount, if not specified @mountpoint entry
 *                        from fstab will be used
 * @mountpoint: (allow-none): mountpoint for @device, if not specified @device
 *                            entry from fstab will be used
 * @fstype: (allow-none): filesystem type
 * @options: (allow-none): comma delimited options for mount
 * @extra: (allow-none) (array zero-terminated=1): extra options for the mount
 *                                                 currently only 'run_as_uid'
 *                                                 and 'run_as_gid' are supported
 *                                                 value must be a valid non zero
 *                                                 uid (gid)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether @device (or @mountpoint) was successfully mounted or not
 *
 * Tech category: %BD_FS_TECH_MOUNT (no mode, ignored)
 */
gboolean bd_fs_mount (const gchar *device, const gchar *mountpoint, const gchar *fstype, const gchar *options, const BDExtraArg **extra, GError **error) {
    uid_t run_as_uid = -1;
    gid_t run_as_gid = -1;
    uid_t current_uid = -1;
    gid_t current_gid = -1;
    const BDExtraArg **extra_p = NULL;
    gchar *endptr = NULL;
    MountArgs args;

    args.device = device;
    args.mountpoint = mountpoint;
    args.fstype = fstype;
    args.options = options;

    current_uid = getuid ();
    run_as_uid = current_uid;

    current_gid = getgid ();
    run_as_gid = current_gid;

    if (extra) {
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "run_as_uid") == 0)) {
                run_as_uid = g_ascii_strtoull ((*extra_p)->val, &endptr, 0);

                /* g_ascii_strtoull returns 0 in case of error */
                if (run_as_uid == 0 && endptr == (*extra_p)->val) {
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Invalid specification of UID: '%s'", (*extra_p)->val);
                    return FALSE;
                }
            } else if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "run_as_gid") == 0)) {
                run_as_gid = g_ascii_strtoull ((*extra_p)->val, &endptr, 0);

                /* g_ascii_strtoull returns 0 in case of error */
                if (run_as_gid == 0 && endptr == (*extra_p)->val) {
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Invalid specification of GID: '%s'", (*extra_p)->val);
                    return FALSE;
                }
            } else {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unsupported argument for unmount: '%s'", (*extra_p)->opt);
                return FALSE;
            }
        }
    }

    if (run_as_uid != current_uid || run_as_gid != current_gid) {
        return run_as_user ((MountFunc) do_mount, &args, run_as_uid, run_as_gid, error);
    } else
       return do_mount (&args, error);

    return TRUE;
}

/**
 * bd_fs_get_mountpoint:
 * @device: device to find mountpoint for
 * @error: (out): place to store error (if any)
 *
 * Get mountpoint for @device. If @device is mounted multiple times only
 * one mountpoint will be returned.
 *
 * Returns: (transfer full): mountpoint for @device, %NULL in case device is
 *                           not mounted or in case of an error (@error is set
 *                           in this case)
 *
 * Tech category: %BD_FS_TECH_MOUNT (no mode, ignored)
 */
gchar* bd_fs_get_mountpoint (const gchar *device, GError **error) {
    struct libmnt_table *table = NULL;
    struct libmnt_fs *fs = NULL;
    struct libmnt_cache *cache = NULL;
    gint ret = 0;
    gchar *mountpoint = NULL;
    const gchar *target = NULL;

    table = mnt_new_table ();
    cache = mnt_new_cache ();

    ret = mnt_table_set_cache (table, cache);
    if (ret != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to set cache for mount info table.");
        mnt_free_table (table);
        return NULL;
    }

    ret = mnt_table_parse_mtab (table, NULL);
    if (ret != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to parse mount info.");
        mnt_free_table (table);
        mnt_free_cache (cache);
        return NULL;
    }

    fs = mnt_table_find_source (table, device, MNT_ITER_FORWARD);
    if (!fs) {
        mnt_free_table (table);
        mnt_free_cache (cache);
        return NULL;
    }

    target = mnt_fs_get_target (fs);
    if (!target) {
        mnt_free_fs (fs);
        mnt_free_table (table);
        mnt_free_cache (cache);
        return NULL;
    }

    mountpoint = g_strdup (target);
    mnt_free_fs (fs);
    mnt_free_table (table);
    mnt_free_cache (cache);
    return mountpoint;
}

/**
 * bd_fs_is_mountpoint:
 * @path: path (folder) to check
 * @error: (out): place to store error (if any)
 *
 * Returns: whether @path is a mountpoint or not
 *
 * Tech category: %BD_FS_TECH_MOUNT (no mode, ignored)
 */
gboolean bd_fs_is_mountpoint (const gchar *path, GError **error) {
    struct libmnt_table *table = NULL;
    struct libmnt_fs *fs = NULL;
    struct libmnt_cache *cache = NULL;
    const gchar *target = NULL;
    gint ret = 0;

    table = mnt_new_table ();
    cache = mnt_new_cache ();

    ret = mnt_table_set_cache (table, cache);
    if (ret != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to set cache for mount info table.");
        mnt_free_table (table);
        return FALSE;
    }

    ret = mnt_table_parse_mtab (table, NULL);
    if (ret != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to parse mount info.");
        mnt_free_table (table);
        mnt_free_cache (cache);
        return FALSE;
    }

    fs = mnt_table_find_target (table, path, MNT_ITER_BACKWARD);
    if (!fs) {
        mnt_free_table (table);
        mnt_free_cache (cache);
        return FALSE;
    }

    target = mnt_fs_get_target (fs);
    if (!target) {
        mnt_free_fs (fs);
        mnt_free_table (table);
        mnt_free_cache (cache);
        return FALSE;
    }

    mnt_free_fs (fs);
    mnt_free_table (table);
    mnt_free_cache (cache);
    return TRUE;
}
