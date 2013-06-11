/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager system settings service - keyfile plugin
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2008 - 2012 Red Hat, Inc.
 */

#include <config.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/ether.h>
#include <string.h>

#include <gmodule.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <nm-connection.h>
#include <nm-setting.h>
#include <nm-setting-connection.h>
#include <nm-utils.h>
#include <nm-config.h>

#include "plugin.h"
#include "nm-system-config-interface.h"
#include "nm-keyfile-connection.h"
#include "writer.h"
#include "common.h"
#include "utils.h"

static char *plugin_get_hostname (SCPluginKeyfile *plugin);
static void system_config_interface_init (NMSystemConfigInterface *system_config_interface_class);

G_DEFINE_TYPE_EXTENDED (SCPluginKeyfile, sc_plugin_keyfile, G_TYPE_OBJECT, 0,
				    G_IMPLEMENT_INTERFACE (NM_TYPE_SYSTEM_CONFIG_INTERFACE,
									  system_config_interface_init))

#define SC_PLUGIN_KEYFILE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SC_TYPE_PLUGIN_KEYFILE, SCPluginKeyfilePrivate))

typedef struct {
	GHashTable *connections;  /* uuid::connection */

	gboolean initialized;
	GFileMonitor *monitor;
	guint monitor_id;

	const char *conf_file;
	GFileMonitor *conf_file_monitor;
	guint conf_file_monitor_id;

	char *hostname;

	gboolean disposed;
} SCPluginKeyfilePrivate;

static NMSettingsConnection *
_internal_new_connection (SCPluginKeyfile *self,
                          const char *full_path,
                          NMConnection *source,
                          GError **error)
{
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (self);
	NMKeyfileConnection *connection;

	connection = nm_keyfile_connection_new (source, full_path, error);
	if (connection) {
		g_hash_table_insert (priv->connections,
		                     (gpointer) nm_connection_get_uuid (NM_CONNECTION (connection)),
		                     connection);
	}

	return (NMSettingsConnection *) connection;
}

static void
read_connections (NMSystemConfigInterface *config)
{
	SCPluginKeyfile *self = SC_PLUGIN_KEYFILE (config);
	GDir *dir;
	GError *error = NULL;
	const char *item;

	dir = g_dir_open (KEYFILE_DIR, 0, &error);
	if (!dir) {
		PLUGIN_WARN (KEYFILE_PLUGIN_NAME, "Cannot read directory '%s': (%d) %s",
		             KEYFILE_DIR,
		             error ? error->code : -1,
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
		return;
	}

	while ((item = g_dir_read_name (dir))) {
		NMSettingsConnection *connection;
		char *full_path;

		if (nm_keyfile_plugin_utils_should_ignore_file (item))
			continue;

		full_path = g_build_filename (KEYFILE_DIR, item, NULL);
		PLUGIN_PRINT (KEYFILE_PLUGIN_NAME, "parsing %s ... ", item);

		connection = _internal_new_connection (self, full_path, NULL, &error);
		if (connection) {
			PLUGIN_PRINT (KEYFILE_PLUGIN_NAME, "    read connection '%s'",
			              nm_connection_get_id (NM_CONNECTION (connection)));
		} else {
			PLUGIN_PRINT (KEYFILE_PLUGIN_NAME, "    error: %s",
				          (error && error->message) ? error->message : "(unknown)");
		}
		g_clear_error (&error);
		g_free (full_path);
	}
	g_dir_close (dir);
}

/* Monitoring */

static void
remove_connection (SCPluginKeyfile *self, NMKeyfileConnection *connection)
{
	g_return_if_fail (connection != NULL);

	/* Removing from the hash table should drop the last reference */
	g_object_ref (connection);
	g_hash_table_remove (SC_PLUGIN_KEYFILE_GET_PRIVATE (self)->connections,
	                     nm_connection_get_uuid (NM_CONNECTION (connection)));
	nm_settings_connection_signal_remove (NM_SETTINGS_CONNECTION (connection));
	g_object_unref (connection);
}

static NMKeyfileConnection *
find_by_path (SCPluginKeyfile *self, const char *path)
{
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (self);
	GHashTableIter iter;
	NMKeyfileConnection *candidate = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer) &candidate)) {
		if (g_str_equal (path, nm_keyfile_connection_get_path (candidate)))
			return candidate;
	}
	return NULL;
}

static void
dir_changed (GFileMonitor *monitor,
             GFile *file,
             GFile *other_file,
             GFileMonitorEvent event_type,
             gpointer user_data)
{
	NMSystemConfigInterface *config = NM_SYSTEM_CONFIG_INTERFACE (user_data);
	SCPluginKeyfile *self = SC_PLUGIN_KEYFILE (config);
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (self);
	char *full_path;
	NMKeyfileConnection *connection, *tmp;
	GError *error = NULL;

	full_path = g_file_get_path (file);
	if (nm_keyfile_plugin_utils_should_ignore_file (full_path)) {
		g_free (full_path);
		return;
	}

	connection = find_by_path (self, full_path);

	switch (event_type) {
	case G_FILE_MONITOR_EVENT_DELETED:
		if (connection) {
			PLUGIN_PRINT (KEYFILE_PLUGIN_NAME, "removed %s.", full_path);
			remove_connection (SC_PLUGIN_KEYFILE (config), connection);
		}
		break;
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		if (connection) {
			/* Update */
			tmp = nm_keyfile_connection_new (NULL, full_path, &error);
			if (tmp) {
				if (!nm_connection_compare (NM_CONNECTION (connection),
				                            NM_CONNECTION (tmp),
				                            NM_SETTING_COMPARE_FLAG_IGNORE_AGENT_OWNED_SECRETS |
				                              NM_SETTING_COMPARE_FLAG_IGNORE_NOT_SAVED_SECRETS)) {
					PLUGIN_PRINT (KEYFILE_PLUGIN_NAME, "updating %s", full_path);
					if (!nm_settings_connection_replace_settings (NM_SETTINGS_CONNECTION (connection),
					                                              NM_CONNECTION (tmp),
					                                              FALSE,  /* don't set Unsaved */
					                                              &error)) {
						/* Shouldn't ever get here as 'new' was verified by the reader already */
						g_assert_no_error (error);
					}
				}
				g_object_unref (tmp);
			} else {
				/* Error; remove the connection */
				PLUGIN_PRINT (KEYFILE_PLUGIN_NAME, "    error: %s",
						      (error && error->message) ? error->message : "(unknown)");
				g_clear_error (&error);
				remove_connection (SC_PLUGIN_KEYFILE (config), connection);
			}
		} else {
			/* New */
			PLUGIN_PRINT (KEYFILE_PLUGIN_NAME, "updating %s", full_path);
			tmp = nm_keyfile_connection_new (NULL, full_path, &error);
			if (tmp) {
				/* Connection renames will show as different paths but same UUID */
				connection = g_hash_table_lookup (priv->connections, nm_connection_get_uuid (NM_CONNECTION (tmp)));
				if (connection) {
					if (!nm_settings_connection_replace_settings (NM_SETTINGS_CONNECTION (connection),
					                                              NM_CONNECTION (tmp),
					                                              FALSE,  /* don't set Unsaved */
					                                              &error)) {
						/* Shouldn't ever get here as 'tmp' was verified by the reader already */
						g_assert_no_error (error);
					}
					g_object_unref (tmp);
					nm_keyfile_connection_set_path (connection, full_path);
				} else {
					g_hash_table_insert (priv->connections,
					                     (gpointer) nm_connection_get_uuid (NM_CONNECTION (tmp)),
					                     tmp);
					g_signal_emit_by_name (config, NM_SYSTEM_CONFIG_INTERFACE_CONNECTION_ADDED, tmp);
				}
			} else {
				PLUGIN_PRINT (KEYFILE_PLUGIN_NAME, "    error: %s",
						      (error && error->message) ? error->message : "(unknown)");
				g_clear_error (&error);
			}
		}
		break;
	default:
		break;
	}

	g_free (full_path);
}

static void
conf_file_changed (GFileMonitor *monitor,
				   GFile *file,
				   GFile *other_file,
				   GFileMonitorEvent event_type,
				   gpointer data)
{
	SCPluginKeyfile *self = SC_PLUGIN_KEYFILE (data);
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (self);
	char *tmp;

	switch (event_type) {
	case G_FILE_MONITOR_EVENT_DELETED:
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		g_signal_emit_by_name (self, NM_SYSTEM_CONFIG_INTERFACE_UNMANAGED_SPECS_CHANGED);

		/* hostname */
		tmp = plugin_get_hostname (self);
		if ((tmp && !priv->hostname)
			|| (!tmp && priv->hostname)
			|| (priv->hostname && tmp && strcmp (priv->hostname, tmp))) {

			g_free (priv->hostname);
			priv->hostname = tmp;
			tmp = NULL;
			g_object_notify (G_OBJECT (self), NM_SYSTEM_CONFIG_INTERFACE_HOSTNAME);
		}

		g_free (tmp);

		break;
	default:
		break;
	}
}

static void
setup_monitoring (NMSystemConfigInterface *config)
{
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (config);
	GFile *file;
	GFileMonitor *monitor;

	file = g_file_new_for_path (KEYFILE_DIR);
	monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL, NULL);
	g_object_unref (file);

	if (monitor) {
		priv->monitor_id = g_signal_connect (monitor, "changed", G_CALLBACK (dir_changed), config);
		priv->monitor = monitor;
	}

	if (priv->conf_file) {
		file = g_file_new_for_path (priv->conf_file);
		monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, NULL);
		g_object_unref (file);

		if (monitor) {
			priv->conf_file_monitor_id = g_signal_connect (monitor, "changed", G_CALLBACK (conf_file_changed), config);
			priv->conf_file_monitor = monitor;
		}
	}
}

/* Plugin */

static GSList *
get_connections (NMSystemConfigInterface *config)
{
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (config);
	GHashTableIter iter;
	gpointer data = NULL;
	GSList *list = NULL;

	if (!priv->initialized) {
		setup_monitoring (config);
		read_connections (config);
		priv->initialized = TRUE;
	}

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, NULL, &data))
		list = g_slist_prepend (list, data);
	return list;
}

static NMSettingsConnection *
add_connection (NMSystemConfigInterface *config,
                NMConnection *connection,
                gboolean save_to_disk,
                GError **error)
{
	SCPluginKeyfile *self = SC_PLUGIN_KEYFILE (config);
	NMSettingsConnection *added = NULL;
	char *path = NULL;

	if (save_to_disk) {
		if (!nm_keyfile_plugin_write_connection (connection, NULL, &path, error))
			return NULL;
	}
		
	added = _internal_new_connection (self, path, connection, error);
	g_free (path);
	return added;
}

static gboolean
parse_key_file_allow_none (SCPluginKeyfilePrivate  *priv,
                           GKeyFile                *key_file,
                           GError                 **error)
{
	gboolean ret = FALSE;
	GError *local_error = NULL;

	if (!g_key_file_load_from_file (key_file, priv->conf_file, G_KEY_FILE_NONE, &local_error)) {
		if (g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			g_clear_error (&local_error);
		else {
			g_propagate_prefixed_error (error, local_error,
			                            "Error parsing file '%s': ",
			                            priv->conf_file);
			goto out;
		}
	}
	ret = TRUE;

 out:
	return ret;
}

static GSList *
get_unmanaged_specs (NMSystemConfigInterface *config)
{
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (config);
	GKeyFile *key_file;
	GSList *specs = NULL;
	GError *error = NULL;
	char *str;

	if (!priv->conf_file)
		return NULL;

	key_file = g_key_file_new ();
	if (!parse_key_file_allow_none (priv, key_file, &error))
		goto out;

	str = g_key_file_get_value (key_file, "keyfile", "unmanaged-devices", NULL);
	if (str) {
		char **udis;
		int i;

		udis = g_strsplit (str, ";", -1);
		g_free (str);

		for (i = 0; udis[i] != NULL; i++) {
			/* Verify unmanaged specification and add it to the list */
			if (!strncmp (udis[i], "mac:", 4) && nm_utils_hwaddr_valid (udis[i] + 4)) {
				specs = g_slist_append (specs, udis[i]);
			} else if (!strncmp (udis[i], "interface-name:", 15) && nm_utils_iface_valid_name (udis[i] + 15)) {
				specs = g_slist_append (specs, udis[i]);
			} else {
				g_warning ("Error in file '%s': invalid unmanaged-devices entry: '%s'", priv->conf_file, udis[i]);
				g_free (udis[i]);
			}
		}

		g_free (udis); /* Yes, g_free, not g_strfreev because we need the strings in the list */
	}

 out:
	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}
	if (key_file)
		g_key_file_free (key_file);

	return specs;
}

static char *
plugin_get_hostname (SCPluginKeyfile *plugin)
{
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (plugin);
	GKeyFile *key_file;
	char *hostname = NULL;
	GError *error = NULL;

	if (!priv->conf_file)
		return NULL;

	key_file = g_key_file_new ();
	if (!parse_key_file_allow_none (priv, key_file, &error))
		goto out;

	hostname = g_key_file_get_value (key_file, "keyfile", "hostname", NULL);

 out:
	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}
	if (key_file)
		g_key_file_free (key_file);

	return hostname;
}

static gboolean
plugin_set_hostname (SCPluginKeyfile *plugin, const char *hostname)
{
	gboolean ret = FALSE;
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (plugin);
	GKeyFile *key_file = NULL;
	GError *error = NULL;
	char *data = NULL;
	gsize len;

	if (!priv->conf_file) {
		g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Error saving hostname: no config file");
		goto out;
	}

	g_free (priv->hostname);
	priv->hostname = g_strdup (hostname);

	key_file = g_key_file_new ();
	if (!parse_key_file_allow_none (priv, key_file, &error))
		goto out;

	g_key_file_set_string (key_file, "keyfile", "hostname", hostname);

	data = g_key_file_to_data (key_file, &len, &error);
	if (!data)
		goto out;

	if (!g_file_set_contents (priv->conf_file, data, len, &error)) {
		g_prefix_error (&error, "Error saving hostname: ");
		goto out;
	}

	ret = TRUE;

 out:
	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}
	g_free (data);
	if (key_file)
		g_key_file_free (key_file);

	return ret;
}

/* GObject */

static void
sc_plugin_keyfile_init (SCPluginKeyfile *plugin)
{
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (plugin);

	priv->connections = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
}

static void
get_property (GObject *object, guint prop_id,
		    GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	case NM_SYSTEM_CONFIG_INTERFACE_PROP_NAME:
		g_value_set_string (value, KEYFILE_PLUGIN_NAME);
		break;
	case NM_SYSTEM_CONFIG_INTERFACE_PROP_INFO:
		g_value_set_string (value, KEYFILE_PLUGIN_INFO);
		break;
	case NM_SYSTEM_CONFIG_INTERFACE_PROP_CAPABILITIES:
		g_value_set_uint (value, NM_SYSTEM_CONFIG_INTERFACE_CAP_MODIFY_CONNECTIONS | 
						  NM_SYSTEM_CONFIG_INTERFACE_CAP_MODIFY_HOSTNAME);
		break;
	case NM_SYSTEM_CONFIG_INTERFACE_PROP_HOSTNAME:
		g_value_set_string (value, SC_PLUGIN_KEYFILE_GET_PRIVATE (object)->hostname);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object, guint prop_id,
			  const GValue *value, GParamSpec *pspec)
{
	const char *hostname;

	switch (prop_id) {
	case NM_SYSTEM_CONFIG_INTERFACE_PROP_HOSTNAME:
		hostname = g_value_get_string (value);
		if (hostname && strlen (hostname) < 1)
			hostname = NULL;
		plugin_set_hostname (SC_PLUGIN_KEYFILE (object), hostname);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
dispose (GObject *object)
{
	SCPluginKeyfilePrivate *priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (object);

	if (priv->disposed)
		return;

	priv->disposed = TRUE;

	if (priv->monitor) {
		if (priv->monitor_id)
			g_signal_handler_disconnect (priv->monitor, priv->monitor_id);

		g_file_monitor_cancel (priv->monitor);
		g_object_unref (priv->monitor);
	}

	if (priv->conf_file_monitor) {
		if (priv->conf_file_monitor_id)
			g_signal_handler_disconnect (priv->conf_file_monitor, priv->conf_file_monitor_id);

		g_file_monitor_cancel (priv->conf_file_monitor);
		g_object_unref (priv->conf_file_monitor);
	}

	g_free (priv->hostname);

	if (priv->connections) {
		g_hash_table_destroy (priv->connections);
		priv->connections = NULL;
	}

	G_OBJECT_CLASS (sc_plugin_keyfile_parent_class)->dispose (object);
}

static void
sc_plugin_keyfile_class_init (SCPluginKeyfileClass *req_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (req_class);

	g_type_class_add_private (req_class, sizeof (SCPluginKeyfilePrivate));

	object_class->dispose = dispose;
	object_class->get_property = get_property;
	object_class->set_property = set_property;

	g_object_class_override_property (object_class,
	                                  NM_SYSTEM_CONFIG_INTERFACE_PROP_NAME,
	                                  NM_SYSTEM_CONFIG_INTERFACE_NAME);

	g_object_class_override_property (object_class,
	                                  NM_SYSTEM_CONFIG_INTERFACE_PROP_INFO,
	                                  NM_SYSTEM_CONFIG_INTERFACE_INFO);

	g_object_class_override_property (object_class,
	                                  NM_SYSTEM_CONFIG_INTERFACE_PROP_CAPABILITIES,
	                                  NM_SYSTEM_CONFIG_INTERFACE_CAPABILITIES);

	g_object_class_override_property (object_class,
	                                  NM_SYSTEM_CONFIG_INTERFACE_PROP_HOSTNAME,
	                                  NM_SYSTEM_CONFIG_INTERFACE_HOSTNAME);
}

static void
system_config_interface_init (NMSystemConfigInterface *system_config_interface_class)
{
	/* interface implementation */
	system_config_interface_class->get_connections = get_connections;
	system_config_interface_class->add_connection = add_connection;
	system_config_interface_class->get_unmanaged_specs = get_unmanaged_specs;
}

GObject *
nm_settings_keyfile_plugin_new (void)
{
	static SCPluginKeyfile *singleton = NULL;
	SCPluginKeyfilePrivate *priv;

	if (!singleton) {
		singleton = SC_PLUGIN_KEYFILE (g_object_new (SC_TYPE_PLUGIN_KEYFILE, NULL));
		priv = SC_PLUGIN_KEYFILE_GET_PRIVATE (singleton);

		priv->conf_file = nm_config_get_path (nm_config_get ());

		/* plugin_set_hostname() has to be called *after* priv->conf_file is set */
		priv->hostname = plugin_get_hostname (singleton);
	} else
		g_object_ref (singleton);

	return G_OBJECT (singleton);
}
