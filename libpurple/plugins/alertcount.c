#define _GNU_SOURCE
#include "internal.h"
#include "debug.h"
#include "plugin.h"
#include "version.h"

/** Plugin id : type-author-name (to guarantee uniqueness) */
#define ALERTS_PLUGIN_ID "core-dylex-alertcount"

static int Listener = -1;
static guint Incoming = 0;
static int Connection = -1;
static guint Outgoing = 0;
static int Count = -1;

#ifdef ACTIVITY
static struct activity {
	struct activity *next;
	PurpleConversation *conv;
	uint16_t count;
	uint8_t ident;
} *Activity = NULL;
#endif

static gboolean
conversation_ignore(PurpleConversation *conv)
{
	return !strcmp(conv->name, "slackbot")
		|| !strcmp(conv->name, "NickServ");
}

static uint8_t
conversation_ident(PurpleConversation *conv)
{
	unsigned h;
	/* hack to get rid of variables resource markers (jabber only?) */
	char *p = strchrnul(strchrnul(conv->name, '@'), '/');
	char o = *p;
	*p = '\0';
	h = g_str_hash(conv->name);
	*p = o;
	while (!(h & 0x7F) && h)
		h >>= 7;
	if (!h)
		h = 0x7F;
	return (conv->type << 7) | (h & 0x7F);
}

#ifdef ACTIVITY
static void
activity_update(PurpleConversation *conv, int count)
{
	struct activity **p, *a;
	for (p = &Activity; *p && (*p)->conv != conv; p = &(*p)->next);
	if ((a = *p)) {
		if (count)
			a->count = count;
		else {
			*p = a->next;
			g_slice_free(struct activity, a);
		}
	} else if (count) {
		a = *p = g_slice_new(struct activity);
		a->next = NULL;
		a->conv = conv;
		a->count = count;
		a->ident = conversation_ident(conv);
	}
}
#endif

static unsigned
alerts_get_count(uint8_t *buf, size_t buflen)
{
	const gboolean aggregate = purple_prefs_get_bool("/plugins/core/alertcount/aggregate");
	unsigned count = 0;
	GList *l;
	if (!aggregate)
		buflen &= ~1;

	for (l = purple_get_conversations(); l != NULL; l = l->next) {
		PurpleConversation *conv = (PurpleConversation *)l->data;
		if (conversation_ignore(conv))
			continue;
		int c = GPOINTER_TO_INT(purple_conversation_get_data(conv, "unseen-count"));
		if (!c)
			continue;
		if (count < buflen) {
			buf[count++] = conversation_ident(conv);
			if (!aggregate)
				buf[count++] = c;
		} else if (aggregate)
			count ++;
		else
			count += c;
	}
	Count = count;
	return count;
}

static void
alerts_outgoing_cb(gpointer data, gint source, PurpleInputCondition cond)
{
	const gboolean ident = purple_prefs_get_bool("/plugins/core/alertcount/ident");
	unsigned c;
	uint8_t buf[128];

	if (Outgoing) {
		purple_input_remove(Outgoing);
		Outgoing = 0;
	}

	c = alerts_get_count(buf, ident ? 127 : 0);
	if (c > 127)
		c = 127;
	if (ident)
		buf[c++] = 0;
	else {
		*buf = c;
		c = 1;
	}
	if (send(Connection, buf, c, 0) <= 0) { /* ignore short reads */
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
	int c;

	if (!(type & PURPLE_CONV_UPDATE_UNSEEN))
		return;
	if (Connection < 0 || Outgoing)
		return;
	if (conversation_ignore(conv))
		return;
	c = GPOINTER_TO_INT(purple_conversation_get_data(conv, "unseen-count"));
	if (!Count && !c)
		return;
#ifdef ACTIVITY
	activity_update(conv, c);
#endif
	Outgoing = purple_input_add(Connection, PURPLE_INPUT_WRITE, alerts_outgoing_cb, NULL);
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	const int on = 1;
	const struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(purple_prefs_get_int("/plugins/core/alertcount/port")),
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
#ifdef ACTIVITY
	if (Activity) {
		g_slice_free_chain(struct activity, Activity, next);
		Activity = NULL;
	}
#endif
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

static PurplePluginPrefFrame *
get_plugin_pref_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *ppref;

	frame = purple_plugin_pref_frame_new();

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/alertcount/port", _("Listen on _port"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/alertcount/aggregate", _("_Aggregate conversation"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/alertcount/ident", _("_Identify conversations"));
	purple_plugin_pref_frame_add(frame, ppref);

	return frame;
}

static PurplePluginUiInfo prefs_info =
{
	get_plugin_pref_frame,
	0,   /* page_num (Reserved) */
	NULL, /* frame (Reserved) */
	NULL,
	NULL,
	NULL,
	NULL
};

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
	&prefs_info,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static void
init_plugin(PurplePlugin *plugin)
{
	purple_prefs_add_none("/plugins/core/alertcount");
	purple_prefs_add_int("/plugins/core/alertcount/port", 6652);
	purple_prefs_add_bool("/plugins/core/alertcount/aggregate", TRUE);
	purple_prefs_add_bool("/plugins/core/alertcount/ident", FALSE);
}

PURPLE_INIT_PLUGIN(alertcount, init_plugin, info)
