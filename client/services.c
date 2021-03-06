/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2012  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "client/services.h"
#include "src/connman.h"

static void append_property_array(DBusMessageIter *iter, char *property,
						char **data, int num_args)
{
	DBusMessageIter value, array;
	int i;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &property);

	connman_dbus_array_open(iter, &value);
	dbus_message_iter_open_container(&value, DBUS_TYPE_ARRAY,
					 DBUS_TYPE_STRING_AS_STRING, &array);

	for (i = 0; i < num_args; i++) {
		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING,
						&data[i]);
		printf("Added: %s\n", data[i]);
	}
	dbus_message_iter_close_container(&value, &array);
	dbus_message_iter_close_container(iter, &value);
}

static void append_property_dict(DBusMessageIter *iter, char *property,
					char **keys, char **data, int num_args)
{
	DBusMessageIter value, dict, entry, dict_key;
	int i;
	unsigned char prefix;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &property);

	/* Top most level is a{sv} */
	connman_dbus_dict_open_variant(iter, &value);

	connman_dbus_dict_open(&value, &dict);

	for (i = 0; i < num_args; i++) {
		dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY,
							NULL, &entry);
		dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING,
							&keys[i]);

		if (strcmp(property, "IPv6.Configuration") == 0 &&
					   g_strcmp0(keys[i], "PrefixLength")) {
			if (data[i] == NULL) {
				fprintf(stderr, "No values entered!\n");
				exit(EXIT_FAILURE);
			}
			prefix = atoi(data[i]);

			dbus_message_iter_open_container(&entry,
						DBUS_TYPE_VARIANT,
						DBUS_TYPE_BYTE_AS_STRING,
						&dict_key);
			dbus_message_iter_append_basic(&dict_key,
						       DBUS_TYPE_BYTE, &prefix);
		} else {
			dbus_message_iter_open_container(&entry,
						 DBUS_TYPE_VARIANT,
						 DBUS_TYPE_STRING_AS_STRING,
						 &dict_key);
			dbus_message_iter_append_basic(&dict_key,
							DBUS_TYPE_STRING,
							&data[i]);
		}
		dbus_message_iter_close_container(&entry, &dict_key);
		dbus_message_iter_close_container(&dict, &entry);
	}
	/* Close {sv}, then close a{sv} */
	connman_dbus_dict_close(&value, &dict);
	connman_dbus_dict_close(iter, &value);
}

void iterate_array(DBusMessageIter *iter)
{
	DBusMessageIter array_item;
	dbus_bool_t key_bool;
	char *key_str;

	dbus_message_iter_recurse(iter, &array_item);
	/* Make sure the entry is not NULL! */
	printf("[ ");
	while (dbus_message_iter_get_arg_type(&array_item) !=
					DBUS_TYPE_INVALID) {
		if (dbus_message_iter_get_arg_type(&array_item) ==
					DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&array_item,
						&key_str);
			printf("%s ", key_str);
		} else if (dbus_message_iter_get_arg_type(&array_item) ==
					DBUS_TYPE_BOOLEAN) {
			dbus_message_iter_get_basic(&array_item, &key_bool);
			printf("%s ", key_bool == TRUE ? "True"
						: "False");
		}
		dbus_message_iter_next(&array_item);
	}
	if (dbus_message_iter_get_arg_type(&array_item) ==
					DBUS_TYPE_INVALID)
		printf("] ");
}

void iterate_dict(DBusMessageIter *dict, char *string, uint16_t key_int)
{
	DBusMessageIter dict_entry, sub_dict_entry;

	printf("{ ");
	while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY) {
		dbus_message_iter_recurse(dict, &dict_entry);
		dbus_message_iter_get_basic(&dict_entry, &string);
		printf("%s=", string);
		dbus_message_iter_next(&dict_entry);
		while (dbus_message_iter_get_arg_type(&dict_entry)
							!= DBUS_TYPE_INVALID) {
			dbus_message_iter_recurse(&dict_entry, &sub_dict_entry);
			if (dbus_message_iter_get_arg_type(&sub_dict_entry)
							== DBUS_TYPE_UINT16) {
				dbus_message_iter_get_basic(&sub_dict_entry,
								&key_int);
				printf("%d ", key_int);
			} else if (dbus_message_iter_get_arg_type(&sub_dict_entry)
							== DBUS_TYPE_STRING) {
				dbus_message_iter_get_basic(&sub_dict_entry,
								&string);
				printf("%s ", string);
			} else if (dbus_message_iter_get_arg_type(&sub_dict_entry)
							== DBUS_TYPE_ARRAY) {
				iterate_array(&sub_dict_entry);
			}
			dbus_message_iter_next(&dict_entry);
		}
		dbus_message_iter_next(dict);
	}
	printf("}");
}

/* Get dictionary info about the current service and store it */
static void extract_service_properties(DBusMessageIter *dict,
				struct service_data *service)
{
	DBusMessageIter entry, value, array_item;
	char *key;
	char *key_str;
	uint16_t key_uint16;
	dbus_bool_t key_bool;

	while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY) {
		dbus_message_iter_recurse(dict, &entry);
		dbus_message_iter_get_basic(&entry, &key);
		printf("\n  %s = ", key);
		if (strcmp(key, "Name") == 0 && strlen(key) < 5)
			service->name = key;

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);
		/* Check if entry is a dictionary itself */
		if (strcmp(key, "Ethernet") == 0 ||
			/* if just strcmp, the .Configuration names don't match
			 * and they are iterated with iterate_array instead*/
				strncmp(key, "IPv4", 4) == 0 ||
				strncmp(key, "IPv6", 4) == 0 ||
				strncmp(key, "Proxy", 5) == 0 ||
				strcmp(key, "Provider") == 0) {
			dbus_message_iter_recurse(&value, &array_item);
			iterate_dict(&array_item, key_str, key_uint16);
		} else
		switch (dbus_message_iter_get_arg_type(&value)) {
		case DBUS_TYPE_ARRAY:
			iterate_array(&value);
			break;
		case DBUS_TYPE_BOOLEAN:
			dbus_message_iter_get_basic(&value, &key_bool);
			printf("%s", key_bool == TRUE ? "True" : "False");
			break;
		case DBUS_TYPE_BYTE:
			dbus_message_iter_get_basic(&value, &key_uint16);
			printf("%d", key_uint16);
			break;
		case DBUS_TYPE_STRING:
			dbus_message_iter_get_basic(&value, &key_str);
			printf("%s", key_str);
			break;
		}
		dbus_message_iter_next(dict);
	}
	printf("\n\n");
}

static void match_service_name(DBusMessage *message, char *service_name,
						struct service_data *service)
{
	DBusMessageIter iter, array;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_recurse(&iter, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
		DBusMessageIter entry, dict;
		char *path;

		dbus_message_iter_recurse(&array, &entry);
		dbus_message_iter_get_basic(&entry, &path);

		service->path = strip_service_path(path);
		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &dict);
		extract_service_name(&dict, service);
		if (g_strcmp0(service_name, service->name) == 0) {
			printf("    Matched %s with %s\n\n", service->name,
								service->path);
			break;
		}
		dbus_message_iter_next(&array);
	}
}

void extract_service_name(DBusMessageIter *dict, struct service_data *service)
{
	DBusMessageIter dict_entry, value;
	const char *key;
	const char *state;

	while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY) {
		dbus_message_iter_recurse(dict, &dict_entry);
		dbus_message_iter_get_basic(&dict_entry, &key);
		if (strcmp(key, "Name") == 0) {
			dbus_message_iter_next(&dict_entry);
			dbus_message_iter_recurse(&dict_entry, &value);
			dbus_message_iter_get_basic(&value, &service->name);
		}
		if (strcmp(key, "AutoConnect") == 0) {
			dbus_message_iter_next(&dict_entry);
			dbus_message_iter_recurse(&dict_entry, &value);
			dbus_message_iter_get_basic(&value, &service->autoconn);
		}
		if (strcmp(key, "State") == 0) {
			dbus_message_iter_next(&dict_entry);
			dbus_message_iter_recurse(&dict_entry, &value);
			dbus_message_iter_get_basic(&value, &state);
			if (strcmp(state, "ready") == 0) {
				service->connected = TRUE;
				service->online = FALSE;
			} else if (strcmp(state, "online") == 0) {
				service->connected = FALSE;
				service->online = TRUE;
			} else {
				service->connected = FALSE;
				service->online = FALSE;
			}
		}
		if (strcmp(key, "Favorite") == 0) {
			dbus_message_iter_next(&dict_entry);
			dbus_message_iter_recurse(&dict_entry, &value);
			dbus_message_iter_get_basic(&value, &service->favorite);
		}
		dbus_message_iter_next(dict);
	}
}

/* Show detailed information about a service */
void extract_services(DBusMessage *message, char *service_name)
{
	DBusMessageIter iter, array;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_recurse(&iter, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
		DBusMessageIter entry, dict;
		struct service_data service;
		char *path;

		dbus_message_iter_recurse(&array, &entry);
		dbus_message_iter_get_basic(&entry, &path);

		service.path = strip_service_path(path);
		if (g_strcmp0(service.path, service_name) == 0) {
			printf("[ %s ]\n", service.path);
			dbus_message_iter_next(&entry);
			dbus_message_iter_recurse(&entry, &dict);
			extract_service_properties(&dict, &service);
		}
		dbus_message_iter_next(&array);
	}
}

/* Support both string names and path names for connecting to services */
char *strip_service_path(char *service)
{
	char *service_name;
	service_name = strrchr(service, '/');
	if (service_name == NULL)
		return service;
	else
		return service_name + 1;
}

/* Show a simple list of service names only */
void get_services(DBusMessage *message)
{
	DBusMessageIter iter, array;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_recurse(&iter, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
		DBusMessageIter entry, dict;
		struct service_data service;
		char *path;

		dbus_message_iter_recurse(&array, &entry);
		dbus_message_iter_get_basic(&entry, &path);

		service.path = strip_service_path(path);
		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &dict);
		extract_service_name(&dict, &service);
		printf("%-1s%-1s%-1s %-20s { %s }\n",
			service.favorite ? "*" : "",
			service.autoconn ? "A" : "",
			service.online ? "O" : (service.connected ? "R" : ""),
			service.name, service.path);
		dbus_message_iter_next(&array);
	}
}

const char *find_service(DBusConnection *connection, DBusMessage *message,
			  char *service_name, struct service_data *service)
{
	DBusMessageIter iter, array, entry;
	char *path;

	service_name = strip_service_path(service_name);
	match_service_name(message, service_name, service);
	/* Service name did not match, so check if entry is a path */
	if (g_strcmp0(service_name, service->name)) {
		dbus_message_iter_init(message, &iter);
		dbus_message_iter_recurse(&iter, &array);

		while (dbus_message_iter_get_arg_type(&array) ==
							DBUS_TYPE_STRUCT) {
			dbus_message_iter_recurse(&array, &entry);
			dbus_message_iter_get_basic(&entry, &path);

			service->path = strip_service_path(path);
			if (g_strcmp0(service->path, service_name) == 0)
				return service->path;
			dbus_message_iter_next(&array);
		}
		fprintf(stderr, "'%s' is not a valid service name or path.\n",
								service_name);
		fprintf(stderr, "Use the 'services' command to find available "
							"services.\n");
		return NULL;
	} else
		return service->path;
}

int set_proxy_manual(DBusConnection *connection, DBusMessage *message,
				char *name, char **servers, char **excludes,
				int num_servers, int num_excludes)
{
	DBusMessage *message_send;
	DBusMessageIter iter, value, dict, entry, data;
	struct service_data service;
	char *path;
	const char *path_name;
	char *property = "Proxy.Configuration";
	char *method = "Method";
	char *manual = "manual";

	path_name = find_service(connection, message, name, &service);
	if (path_name == NULL)
		return -ENXIO;

	path = g_strdup_printf("/net/connman/service/%s", path_name);
	message_send = dbus_message_new_method_call("net.connman", path,
							"net.connman.Service",
							"SetProperty");

	if (message_send == NULL) {
		g_free(path);
		return -ENOMEM;
	}

	dbus_message_iter_init_append(message_send, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &property);
	connman_dbus_dict_open_variant(&iter, &value);
	connman_dbus_dict_open(&value, &dict);
	dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL,
							&entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &method);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
						DBUS_TYPE_STRING_AS_STRING,
						&data);
	dbus_message_iter_append_basic(&data, DBUS_TYPE_STRING, &manual);
	dbus_message_iter_close_container(&entry, &data);
	dbus_message_iter_close_container(&dict, &entry);
	dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL,
							&entry);
	append_property_array(&entry, "Servers", servers, num_servers);
	dbus_message_iter_close_container(&dict, &entry);

	if (num_excludes != 0) {
		dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY,
							NULL, &entry);
		append_property_array(&entry, "Excludes", excludes,
							num_excludes);
		dbus_message_iter_close_container(&dict, &entry);
	}

	dbus_message_iter_close_container(&value, &dict);
	dbus_message_iter_close_container(&iter, &value);
	dbus_connection_send(connection, message_send, NULL);
	dbus_connection_flush(connection);
	dbus_message_unref(message_send);

	g_free(path);

	return 0;
}

int set_service_property(DBusConnection *connection, DBusMessage *message,
				char *name, char *property, char **keys,
				void *data, int num_args)
{
	DBusMessage *message_send;
	DBusMessageIter iter;
	struct service_data service;
	char *path;
	const char *path_name;

	path_name = find_service(connection, message, name, &service);
	if (path_name == NULL)
		return -ENXIO;

	path = g_strdup_printf("/net/connman/service/%s", path_name);
	message_send = dbus_message_new_method_call("net.connman", path,
							"net.connman.Service",
							"SetProperty");

	if (message_send == NULL) {
		g_free(path);
		return -ENOMEM;
	}

	dbus_message_iter_init_append(message_send, &iter);

	if (strcmp(property, "AutoConnect") == 0)
		connman_dbus_property_append_basic(&iter,
						(const char *) property,
						DBUS_TYPE_BOOLEAN, data);
	else if ((strcmp(property, "Domains.Configuration") == 0)
			|| (strcmp(property, "Timeservers.Configuration") == 0)
			|| (strcmp(property, "Nameservers.Configuration") == 0))
		append_property_array(&iter, property, data, num_args);
	else if ((strcmp(property, "IPv4.Configuration") == 0)
			|| (strcmp(property, "IPv6.Configuration") == 0)
			|| (strcmp(property, "Proxy.Configuration") == 0))
		append_property_dict(&iter, property, keys, data, num_args);

	dbus_connection_send(connection, message_send, NULL);
	dbus_connection_flush(connection);
	dbus_message_unref(message_send);
	g_free(path);

	return 0;
}

int remove_service(DBusConnection *connection, DBusMessage *message,
								char *name)
{
	struct service_data service;
	DBusMessage *message_send;
	const char *path_name;
	char *path;

	path_name = find_service(connection, message, name, &service);
	if (path_name == NULL)
		return -ENXIO;

	if (service.favorite == FALSE)
		return 0;

	path = g_strdup_printf("/net/connman/service/%s", path_name);
	message_send = dbus_message_new_method_call(CONNMAN_SERVICE, path,
						CONNMAN_SERVICE_INTERFACE,
						"Remove");
	if (message_send == NULL) {
		g_free(path);
		return -ENOMEM;
	}

	dbus_connection_send(connection, message_send, NULL);
	dbus_message_unref(message_send);
	g_free(path);

	return 0;
}
