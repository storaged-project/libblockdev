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
 *
 * Creates a new copy of @spec.
 */
BDPluginSpec* bd_plugin_spec_copy (BDPluginSpec *spec) {
    BDPluginSpec *new_spec = g_new0 (BDPluginSpec, 1);

    new_spec->name = spec->name;
    new_spec->so_name = g_strdup (spec->so_name);

    return new_spec;
}

/**
 * bd_plugin_spec_free: (skip)
 *
 * Frees @spec.
 */
void bd_plugin_spec_free (BDPluginSpec *spec) {
    g_free ((gchar *) spec->so_name);
    g_free (spec);
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
