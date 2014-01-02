#include "internal.h"
#include "debug.h"
#include "plugin.h"
#include "version.h"

/** Plugin id : type-author-name (to guarantee uniqueness) */
#define ALERTS_PLUGIN_ID "core-dylex-alertcount"

#define ALERTS_PORT	6652

static int Listener = -1;
static guint Incoming = 0;
static int Connection = -1;
static guint Outgoing = 0;

static unsigned
alerts_get_count()
{
	const gboolean aggregate = purple_prefs_get_bool("/plugins/core/alertcount/aggregate");
	unsigned count = 0;
	GList *l;

	for (l = purple_get_conversations(); l != NULL; l = l->next) {
		int c = GPOINTER_TO_INT(purple_conversation_get_data((PurpleConversation *)l->data, "unseen-count"));
		if (aggregate)
			c = !!c;
		count += c;
	}
	return count;
}

static void
alerts_outgoing_cb(gpointer data, gint source, PurpleInputCondition cond)
{
	unsigned count;
	unsigned char c;

	if (Outgoing) {
		purple_input_remove(Outgoing);
		Outgoing = 0;
	}

	count = alerts_get_count();
	if (count > 255)
		c = 255;
	else
		c = count;
	if (send(Connection, &c, sizeof(c), 0) <= 0) {
		purple_debug_info("alertcount", "send: %m\n");
		close(Connection);
		Connection = -1;
	}
}

static void
alerts_incoming_cb(gpointer data, gint source, PurpleInputCondition cond)
{
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);

	if (Outgoing) {
		purple_input_remove(Outgoing);
		Outgoing = 0;
	}
	if (Connection >= 0)
		close(Connection);
	if ((Connection = accept(Listener, (struct sockaddr *)&addr, &addrlen)) < 0)
	{
		purple_debug_warning("alertcount", "accept: %m\n");
		return;
	}

	purple_debug_info("alertcount", "accepted connection from %s:%hu\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
	shutdown(Connection, SHUT_RD);
	fcntl(Connection, F_SETFL, O_NONBLOCK);
	fcntl(Connection, F_SETFD, FD_CLOEXEC);

	Outgoing = purple_input_add(Connection, PURPLE_INPUT_WRITE, alerts_outgoing_cb, NULL);
}

static void
alerts_conversation_cb(PurpleConversation *conv, PurpleConvUpdateType type, void *data)
{
	if (!(type & PURPLE_CONV_UPDATE_UNSEEN))
		return;
	if (Connection < 0 || Outgoing)
		return;
	Outgoing = purple_input_add(Connection, PURPLE_INPUT_WRITE, alerts_outgoing_cb, NULL);
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	const int on = 1;
	const struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(ALERTS_PORT),
		.sin_addr = { htonl(INADDR_LOOPBACK) }
	};

	if ((Listener = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		purple_debug_error("alertcount", "socket: %m\n");
		goto error;
	}
	if (setsockopt(Listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		purple_debug_warning("alertcount", "setsockopt(SO_REUSEADDR): %m\n");
	if (bind(Listener, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		purple_debug_error("alertcount", "bind: %m\n");
		goto error;
	}
	if (listen(Listener, 1) < 0) {
		purple_debug_error("alertcount", "listen: %m\n");
		goto error;
	}

	fcntl(Listener, F_SETFL, O_NONBLOCK);
	fcntl(Listener, F_SETFD, FD_CLOEXEC);

	Incoming = purple_input_add(Listener, PURPLE_INPUT_READ, alerts_incoming_cb, NULL);

	purple_signal_connect(purple_conversations_get_handle(), "conversation-updated",
			plugin, PURPLE_CALLBACK(alerts_conversation_cb), NULL);

	return TRUE;

error:
	if (Listener >= 0) {
		close(Listener);
		Listener = -1;
	}
	return FALSE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	if (Outgoing) {
		purple_input_remove(Outgoing);
		Outgoing = 0;
	}
	if (Incoming) {
		purple_input_remove(Incoming);
		Incoming = 0;
	}
	if (Connection >= 0) {
		close(Connection);
		Connection = -1;
	}
	if (Listener >= 0) {
		close(Listener);
		Listener = -1;
	}
	return TRUE;
}

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,                             /**< type           */
	NULL,                                             /**< ui_requirement */
	0,                                                /**< flags          */
	NULL,                                             /**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,                            /**< priority       */

	ALERTS_PLUGIN_ID,                                 /**< id             */
	N_("Alert Count Server"),                              /**< name           */
	DISPLAY_VERSION,                                  /**< version        */
	                                                  /**  summary        */
	N_("Exports unseen conversation count to a single network listener."),
	                                                  /**  description    */
	N_("Exports unseen conversation count to a single network listener."),
	"Dylan Simon <dylan@dylex.net>",        /**< author         */
	NULL,                                     /**< homepage       */

	plugin_load,                                      /**< load           */
	plugin_unload,                                    /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	NULL,
	NULL
};

static void
init_plugin(PurplePlugin *plugin)
{
	purple_prefs_add_none("/plugins/core/alertcount");
	purple_prefs_add_bool("/plugins/core/alertcount/aggregate", FALSE);
}

PURPLE_INIT_PLUGIN(alertcount, init_plugin, info)
