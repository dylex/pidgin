/**
 * @file signals.c Signal API
 * @ingroup core
 *
 * gaim
 *
 * Copyright (C) 2003 Christian Hammond <chipx86@gnupdate.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "internal.h"

#include "debug.h"
#include "signals.h"

typedef struct
{
	void *instance;

	GHashTable *signals;
	size_t signal_count;

	gulong next_signal_id;

} GaimInstanceData;

typedef struct
{
	gulong id;

	GaimSignalMarshalFunc marshal;

	GList *handlers;
	size_t handler_count;

	gulong next_handler_id;

} GaimSignalData;

typedef struct
{
	gulong id;
	GaimCallback cb;
	void *handle;
	void *data;

} GaimSignalHandlerData;

static GHashTable *instance_table = NULL;

static void
destroy_instance_data(GaimInstanceData *instance_data)
{
	g_hash_table_destroy(instance_data->signals);

	g_free(instance_data);
}

static void
destroy_signal_data(GaimSignalData *signal_data)
{
	GaimSignalHandlerData *handler_data;
	GList *l;

	for (l = signal_data->handlers; l != NULL; l = l->next)
	{
		handler_data = (GaimSignalHandlerData *)l->data;

		g_free(l->data);
	}

	g_list_free(signal_data->handlers);

	g_free(signal_data);
}

gulong
gaim_signal_register(void *instance, const char *signal,
					 GaimSignalMarshalFunc marshal)
{
	GaimInstanceData *instance_data;
	GaimSignalData *signal_data;

	g_return_val_if_fail(instance != NULL, 0);
	g_return_val_if_fail(signal   != NULL, 0);
	g_return_val_if_fail(marshal  != NULL, 0);

	instance_data =
		(GaimInstanceData *)g_hash_table_lookup(instance_table, instance);

	if (instance_data == NULL)
	{
		instance_data = g_new0(GaimInstanceData, 1);

		instance_data->instance = instance;
		instance_data->next_signal_id = 1;

		instance_data->signals =
			g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
								  (GDestroyNotify)destroy_signal_data);

		g_hash_table_insert(instance_table, instance, instance_data);
	}

	signal_data = g_new0(GaimSignalData, 1);
	signal_data->id              = instance_data->next_signal_id;
	signal_data->marshal         = marshal;
	signal_data->next_handler_id = 1;

	g_hash_table_insert(instance_data->signals,
						g_strdup(signal), signal_data);

	instance_data->next_signal_id++;
	instance_data->signal_count++;

	return signal_data->id;
}

void
gaim_signal_unregister(void *instance, const char *signal)
{
	GaimInstanceData *instance_data;

	g_return_if_fail(instance != NULL);
	g_return_if_fail(signal   != NULL);

	instance_data =
		(GaimInstanceData *)g_hash_table_lookup(instance_table, instance);

	g_return_if_fail(instance_data != NULL);

	g_hash_table_remove(instance_data->signals, signal);

	instance_data->signal_count--;

	if (instance_data->signal_count == 0)
	{
		/* Unregister the instance. */
		g_hash_table_remove(instance_table, instance);
	}
}

void
gaim_signals_unregister_by_instance(void *instance)
{
	gboolean found;

	g_return_if_fail(instance != NULL);

	found = g_hash_table_remove(instance_table, instance);

	/*
	 * Makes things easier (more annoying?) for developers who don't have
	 * things registering and unregistering in the right order :)
	 */
	g_return_if_fail(found);
}

gulong
gaim_signal_connect(void *instance, const char *signal, void *handle,
					GaimCallback func, void *data)
{
	GaimInstanceData *instance_data;
	GaimSignalData *signal_data;
	GaimSignalHandlerData *handler_data;

	g_return_val_if_fail(instance != NULL, 0);
	g_return_val_if_fail(signal   != NULL, 0);
	g_return_val_if_fail(handle   != NULL, 0);
	g_return_val_if_fail(func     != NULL, 0);

	/* Get the instance data */
	instance_data =
		(GaimInstanceData *)g_hash_table_lookup(instance_table, instance);

	g_return_val_if_fail(instance_data != NULL, 0);

	/* Get the signal data */
	signal_data =
		(GaimSignalData *)g_hash_table_lookup(instance_data->signals, signal);

	if (signal_data == NULL)
	{
		gaim_debug(GAIM_DEBUG_ERROR, "signals",
				   "Signal data for %s not found!\n", signal);
		return 0;
	}

	/* Create the signal handler data */
	handler_data = g_new0(GaimSignalHandlerData, 1);
	handler_data->id     = signal_data->next_handler_id;
	handler_data->cb     = func;
	handler_data->handle = handle;
	handler_data->data   = data;

	signal_data->handlers = g_list_append(signal_data->handlers, handler_data);
	signal_data->handler_count++;
	signal_data->next_handler_id++;

	return handler_data->id;
}

void
gaim_signal_disconnect(void *instance, const char *signal,
					   void *handle, GaimCallback func)
{
	GaimInstanceData *instance_data;
	GaimSignalData *signal_data;
	GaimSignalHandlerData *handler_data;
	GList *l;
	gboolean found = FALSE;

	g_return_if_fail(instance != NULL);
	g_return_if_fail(signal   != NULL);
	g_return_if_fail(handle   != NULL);
	g_return_if_fail(func     != NULL);

	/* Get the instance data */
	instance_data =
		(GaimInstanceData *)g_hash_table_lookup(instance_table, instance);

	g_return_if_fail(instance_data != NULL);

	/* Get the signal data */
	signal_data =
		(GaimSignalData *)g_hash_table_lookup(instance_data->signals, signal);

	if (signal_data == NULL)
	{
		gaim_debug(GAIM_DEBUG_ERROR, "signals",
				   "Signal data for %s not found!\n", signal);
		return;
	}

	/* Find the handler data. */
	for (l = signal_data->handlers; l != NULL; l = l->next)
	{
		handler_data = (GaimSignalHandlerData *)l->data;

		if (handler_data->handle == handle && handler_data->cb == func)
		{
			g_free(handler_data);

			signal_data->handlers = g_list_remove(signal_data->handlers,
												  handler_data);
			signal_data->handler_count--;

			found = TRUE;

			break;
		}
	}

	/* See note somewhere about this actually helping developers.. */
	g_return_if_fail(found);
}

/*
 * TODO: Make this all more efficient by storing a list of handlers, keyed
 *       to a handle.
 */
static void
disconnect_handle_from_signals(const char *signal,
							   GaimSignalData *signal_data, void *handle)
{
	GList *l, *l_next;
	GaimSignalHandlerData *handler_data;

	for (l = signal_data->handlers; l != NULL; l = l_next)
	{
		handler_data = (GaimSignalHandlerData *)l->data;
		l_next = l->next;

		if (handler_data->handle == handle)
		{
			g_free(handler_data);

			signal_data->handler_count--;
			signal_data->handlers = g_list_remove(signal_data->handlers,
												  handler_data);
		}
	}
}

static void
disconnect_handle_from_instance(void *instance,
								GaimInstanceData *instance_data,
								void *handle)
{
	g_hash_table_foreach(instance_data->signals,
						 (GHFunc)disconnect_handle_from_signals, handle);
}

void
gaim_signals_disconnect_by_handle(void *handle)
{
	g_return_if_fail(handle != NULL);

	g_hash_table_foreach(instance_table,
						 (GHFunc)disconnect_handle_from_instance, handle);
}

void
gaim_signal_emit(void *instance, const char *signal, ...)
{
	va_list args;

	va_start(args, signal);
	gaim_signal_emit_vargs(instance, signal, args);
	va_end(args);
}

void
gaim_signal_emit_vargs(void *instance, const char *signal, va_list args)
{
	GaimInstanceData *instance_data;
	GaimSignalData *signal_data;
	GaimSignalHandlerData *handler_data;
	GList *l;

	g_return_if_fail(instance != NULL);
	g_return_if_fail(signal   != NULL);

	instance_data =
		(GaimInstanceData *)g_hash_table_lookup(instance_table, instance);

	g_return_if_fail(instance_data != NULL);

	signal_data =
		(GaimSignalData *)g_hash_table_lookup(instance_data->signals, signal);

	if (signal_data == NULL)
	{
		gaim_debug(GAIM_DEBUG_ERROR, "signals",
				   "Signal data for %s not found!\n", signal);
		return;
	}

	for (l = signal_data->handlers; l != NULL; l = l->next)
	{
		handler_data = (GaimSignalHandlerData *)l->data;

		signal_data->marshal(handler_data->cb, args, handler_data->data, NULL);
	}
}

void *
gaim_signal_emit_return_1(void *instance, const char *signal, ...)
{
	void *ret_val;
	va_list args;

	va_start(args, signal);
	ret_val = gaim_signal_emit_vargs_return_1(instance, signal, args);
	va_end(args);

	return ret_val;
}

void *
gaim_signal_emit_vargs_return_1(void *instance, const char *signal,
								va_list args)
{
	GaimInstanceData *instance_data;
	GaimSignalData *signal_data;
	GaimSignalHandlerData *handler_data;
	void *ret_val = NULL;
	GList *l;

	g_return_val_if_fail(instance != NULL, NULL);
	g_return_val_if_fail(signal   != NULL, NULL);

	instance_data =
		(GaimInstanceData *)g_hash_table_lookup(instance_table, instance);

	g_return_val_if_fail(instance_data != NULL, NULL);

	signal_data =
		(GaimSignalData *)g_hash_table_lookup(instance_data->signals, signal);

	if (signal_data == NULL)
	{
		gaim_debug(GAIM_DEBUG_ERROR, "signals",
				   "Signal data for %s not found!\n", signal);
		return 0;
	}

	for (l = signal_data->handlers; l != NULL; l = l->next)
	{
		handler_data = (GaimSignalHandlerData *)l->data;

		signal_data->marshal(handler_data->cb, args, handler_data->data,
							 &ret_val);
	}

	return ret_val;
}

void
gaim_signals_init()
{
	g_return_if_fail(instance_table == NULL);

	instance_table =
		g_hash_table_new_full(g_direct_hash, g_direct_equal,
							  NULL, (GDestroyNotify)destroy_instance_data);
}

void
gaim_signals_uninit()
{
	g_return_if_fail(instance_table != NULL);

	g_hash_table_destroy(instance_table);
	instance_table = NULL;
}

/**************************************************************************
 * Marshallers
 **************************************************************************/
void
gaim_marshal_VOID(GaimCallback cb, va_list args, void *data,
				  void **return_val)
{
	((void (*)(void *))cb)(data);
}

void
gaim_marshal_VOID__POINTER(GaimCallback cb, va_list args, void *data,
						   void **return_val)
{
	((void (*)(void *, void *))cb)(va_arg(args, void *), data);
}

void
gaim_marshal_VOID__POINTER_POINTER(GaimCallback cb, va_list args,
								   void *data, void **return_val)
{
	((void (*)(void *, void *, void *))cb)(va_arg(args, void *),
										   va_arg(args, void *),
										   data);
}

void
gaim_marshal_VOID__POINTER_POINTER_UINT(GaimCallback cb, va_list args,
										void *data, void **return_val)
{
	((void (*)(void *, void *, guint, void *))cb)(va_arg(args, void *),
												  va_arg(args, void *),
												  va_arg(args, guint),
												  data);
}

void
gaim_marshal_VOID__POINTER_POINTER_POINTER(GaimCallback cb, va_list args,
										   void *data, void **return_val)
{
	((void (*)(void *, void *, void *, void *))cb)(va_arg(args, void *),
												   va_arg(args, void *),
												   va_arg(args, void *),
												   data);
}

void
gaim_marshal_VOID__POINTER_POINTER_POINTER_POINTER(GaimCallback cb,
												   va_list args,
												   void *data,
												   void **return_val)
{
	((void (*)(void *, void *, void *, void *, void *))cb)(
		va_arg(args, void *), va_arg(args, void *),
		va_arg(args, void *), va_arg(args, void *), data);
}
void
gaim_marshal_VOID__POINTER_POINTER_POINTER_UINT_UINT(GaimCallback cb,
													 va_list args,
													 void *data,
													 void **return_val)
{
	((void (*)(void *, void *, void *, guint, guint, void *))cb)(
			va_arg(args, void *), va_arg(args, void *),
			va_arg(args, void *), va_arg(args, guint),
			va_arg(args, guint), data);
}

void
gaim_marshal_BOOLEAN__POINTER(GaimCallback cb, va_list args, void *data,
							  void **return_val)
{
	gboolean ret_val;

	ret_val = ((gboolean (*)(void *, void *))cb)(va_arg(args, void *), data);

	if (return_val != NULL)
		*return_val = GINT_TO_POINTER(ret_val);
}

void
gaim_marshal_BOOLEAN__POINTER_POINTER(GaimCallback cb, va_list args,
									  void *data, void **return_val)
{
	gboolean ret_val;

	ret_val = ((gboolean (*)(void *, void *, void *))cb)(va_arg(args, void *),
														 va_arg(args, void *),
														 data);

	if (return_val != NULL)
		*return_val = GINT_TO_POINTER(ret_val);
}

void
gaim_marshal_BOOLEAN__POINTER_POINTER_POINTER_UINT(GaimCallback cb,
												   va_list args,
												   void *data,
												   void **return_val)
{
	gboolean ret_val;

	ret_val = ((gboolean (*)(void *, void *, void *, guint, void *))cb)(
			va_arg(args, void *), va_arg(args, void *),
			va_arg(args, void *), va_arg(args, guint),
			data);

	if (return_val != NULL)
		*return_val = GINT_TO_POINTER(ret_val);
}

void
gaim_marshal_BOOLEAN__POINTER_POINTER_POINTER_POINTER(GaimCallback cb,
													  va_list args,
													  void *data,
													  void **return_val)
{
	gboolean ret_val;

	ret_val = ((gboolean (*)(void *, void *, void *, void *, void *))cb)(
			va_arg(args, void *), va_arg(args, void *),
			va_arg(args, void *), va_arg(args, void *),
			data);

	if (return_val != NULL)
		*return_val = GINT_TO_POINTER(ret_val);
}

void
gaim_marshal_BOOLEAN__POINTER_POINTER_POINTER_POINTER_POINTER(
		GaimCallback cb, va_list args, void *data, void **return_val)
{
	gboolean ret_val;

	ret_val =
		((gboolean (*)(void *, void *, void *, void *, void *, void *))cb)(
			va_arg(args, void *), va_arg(args, void *),
			va_arg(args, void *), va_arg(args, void *),
			va_arg(args, void *), data);

	if (return_val != NULL)
		*return_val = GINT_TO_POINTER(ret_val);
}
