#include <glib.h>
#include <glib-object.h>
#include "plugins.h"

/**
 * SECTION: plugins
 * @short_description: functions related to querying plugins
 * @title: Plugins
 * @include: blockdev.h
 */

/**
 * bd_plugin_spec_copy: (skip)
 * @spec: (nullable): %BDPluginSpec to copy
 *
 * Creates a new copy of @spec.
 */
BDPluginSpec* bd_plugin_spec_copy (BDPluginSpec *spec) {
    if (!spec)
        return NULL;

    BDPluginSpec *new_spec = g_new0 (BDPluginSpec, 1);

    new_spec->name = spec->name;
    new_spec->so_name = g_strdup (spec->so_name);

    return new_spec;
}

/**
 * bd_plugin_spec_free: (skip)
 * @spec: (nullable): %BDPluginSpec to free
 *
 * Frees @spec.
 */
void bd_plugin_spec_free (BDPluginSpec *spec) {
    if (!spec)
        return;
    g_free ((gchar *) spec->so_name);
    g_free (spec);
}

/**
 * bd_plugin_spec_new: (constructor)
 * @name: %BDPlugin name, e.g. %BD_PLUGIN_LVM
 * @so_name: (nullable): SO name of the plugin to load or %NULL for default
 *
 * Returns: (transfer full): a new plugin spec
 */
BDPluginSpec* bd_plugin_spec_new (BDPlugin name, const gchar *so_name) {
    BDPluginSpec* ret = g_new0 (BDPluginSpec, 1);
    ret->name = name;
    /* FIXME: this should be const, but we are already allocating a new string in _copy
    *         and freeing it in _free
    */
    ret->so_name = so_name ? g_strdup (so_name) : NULL;

    return ret;
}

GType bd_plugin_spec_get_type (void) {
    static GType type = 0;

    if (G_UNLIKELY(type == 0)) {
        type = g_boxed_type_register_static("BDPluginSpec",
                                            (GBoxedCopyFunc) bd_plugin_spec_copy,
                                            (GBoxedFreeFunc) bd_plugin_spec_free);
    }

    return type;
}
