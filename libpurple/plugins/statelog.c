#include "internal.h"
#include "debug.h"
#include "plugin.h"
#include "version.h"
#include "blist.h"

/** Plugin id : type-author-name (to guarantee uniqueness) */
#define STATELOG_PLUGIN_ID "core-dylex-statelog"

struct BuddyState {
	time_t conn, idle;
	gboolean away;
	char *msg;
};

static GHashTable *buddy_states;

static void buddy_state_free(struct BuddyState *bs)
{
	if (bs->msg)
		g_free(bs->msg);
	g_free(bs);
}

static char *msg_dup(const char *msg)
{
	gsize n = strlen(msg);
	if (!strncasecmp(msg, "<html>", 6))
	{
		msg += 6;
		n -= 6;
	}
	if (!strncasecmp(msg, "<body>", 6))
	{
		msg += 6;
		n -= 6;
	}
	if (n >= 7 && !strncasecmp(msg+n-7, "</html>", 7))
	{
		n -= 7;
	}
	if (n >= 7 && !strncasecmp(msg+n-7, "</body>", 7))
	{
		n -= 7;
	}
	return g_strndup(msg, n);
}

static void
update_state(PurpleBuddy *buddy) // , time_t when, const char *status, const char *msg)
{
	struct BuddyState *curr;
	const char *who;
	char *escaped;
	const PurplePresence *presence;
	const PurpleStatus *status;
	time_t when = 0, conn, idle;
	const char *msg;
	gboolean away;
	gboolean changed = FALSE;
	GString *buf;

	who = purple_buddy_get_alias(buddy);
	escaped = g_markup_escape_text(who, -1);
	buf = g_string_new(escaped);
	g_free(escaped);

	curr = g_hash_table_lookup(buddy_states, buddy);
	if (!curr)
	{
		curr = g_new0(struct BuddyState, 1);
		g_hash_table_insert(buddy_states, buddy, curr);
	}

	presence = purple_buddy_get_presence(buddy);
	status = purple_presence_get_active_status(presence);

	if (purple_presence_is_online(presence))
	{
		if (!(conn = purple_presence_get_login_time(presence)))
			if (!(conn = curr->conn))
				time(&conn);
	}
	else
		conn = 0;
	if (purple_prefs_get_bool("/plugins/core/statelog/log_conn")
			&& conn != curr->conn)
	{
		if (conn)
		{
			when = conn;
			g_string_append(buf, " <font color=\"yellow\">signon</font>");
		}
		else
			g_string_append(buf, " <font color=\"red\">signoff</font>");
		curr->conn = conn;
		changed = TRUE;
	}

	if (purple_presence_is_idle(presence))
	{
		idle = purple_presence_get_idle_time(presence);
		if (!idle) time(&idle);
	}
	else
		idle = 0;
	if (conn && purple_prefs_get_bool("/plugins/core/statelog/log_idle")
			&& idle != curr->idle)
	{
		if (idle)
		{
			if (!when) when = idle;
			g_string_append(buf, " <font color=\"cyan\">idle</font>");
		}
		else
			g_string_append(buf, " unidle");
		curr->idle = idle;
		changed = TRUE;
	}

	if (conn && purple_prefs_get_bool("/plugins/core/statelog/log_away")
			&& (away = !purple_status_is_available(status)) != curr->away)
	{
		if (away)
			g_string_append_printf(buf, " <font color=\"green\">%s</font>", purple_status_get_name(status) ?: "away");
		else
			g_string_append_printf(buf, " %s", purple_status_get_name(status) ?: "back");
		curr->away = away;
		changed = TRUE;
	}

	if (purple_prefs_get_bool("/plugins/core/statelog/log_msg") 
			&& (msg = purple_status_get_attr_string(status, "message"))
			&& (!curr->msg || strcmp(msg, curr->msg)))
	{
		/* XXX clear if msg == NULL? */
		gchar *cmsg = msg_dup(msg);
		if (!curr->msg || strcmp(cmsg, curr->msg))
		{
			purple_debug_info("statelog", "changing msg from '%s' to '%s'\n", curr->msg, cmsg);
			g_string_append_printf(buf, ": %s", cmsg);
			if (curr->msg) g_free(curr->msg);
			curr->msg = cmsg;
			changed = TRUE;
		}
		else
		{
			g_free(cmsg);
		}
	}

	if (!changed)
	{
		g_string_free(buf, TRUE);
		return;
	}

	if (!when) time(&when);

	if (purple_prefs_get_bool("/plugins/core/statelog/to_conv")) {
		PurpleConversation *conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, buddy->name, buddy->account);
		if (conv && conv->type == PURPLE_CONV_TYPE_IM)
			purple_conv_im_write(conv->u.im, NULL, buf->str, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_ACTIVE_ONLY | PURPLE_MESSAGE_NO_LOG, when);
	}

	if (purple_prefs_get_bool("/plugins/core/statelog/collapsed") || !purple_blist_node_get_bool(&purple_buddy_get_group(buddy)->node, "collapsed"))
	{
		if (purple_prefs_get_bool("/plugins/core/statelog/to_acct")) {
			PurpleLog *log = purple_account_get_log(buddy->account, FALSE);
			if (log)
				purple_log_write(log, PURPLE_MESSAGE_SYSTEM, who, when, buf->str);
		}

		if (purple_prefs_get_bool("/plugins/core/statelog/to_self")) {
			PurpleConversation *conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_MISC, "state", buddy->account);
			if (!conv)
				conv = purple_conversation_new(PURPLE_CONV_TYPE_MISC, buddy->account, "state");
			if (conv)
				purple_conversation_write(conv, who, buf->str, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_ACTIVE_ONLY, when);
		}
	}

	g_string_free(buf, TRUE);
}

static void
buddy_status_changed_cb(PurpleBuddy *buddy, PurpleStatus *old_status,
                        PurpleStatus *status, void *data)
{
	update_state(buddy);
}

static void
buddy_idle_changed_cb(PurpleBuddy *buddy, gboolean old_idle, gboolean idle,
                      void *data)
{
	update_state(buddy);
}

static void
buddy_signon_cb(PurpleBuddy *buddy, void *data)
{
	update_state(buddy);
}

static void
buddy_signoff_cb(PurpleBuddy *buddy, void *data)
{
	update_state(buddy);
}

static PurplePluginPrefFrame *
get_plugin_pref_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *ppref;

	frame = purple_plugin_pref_frame_new();

	ppref = purple_plugin_pref_new_with_label(_("Log To"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/statelog/to_conv", _("_Buddy Conversation"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/statelog/to_acct", _("Account _Log"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/statelog/to_self", _("_State Conversation"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_label(_("Log When"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/statelog/log_away", _("Buddy Goes _Away"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/statelog/log_idle", _("Buddy Goes _Idle"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/statelog/log_conn", _("Buddy _Connects/Disconnects"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/statelog/collapsed", _("Buddy in Collapsed _Group"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label("/plugins/core/statelog/log_msg", _("Include Status _Message"));
	purple_plugin_pref_frame_add(frame, ppref);

	return frame;
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	void *blist_handle = purple_blist_get_handle();

	buddy_states = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)buddy_state_free);

	purple_signal_connect(blist_handle, "buddy-status-changed", plugin,
	                    PURPLE_CALLBACK(buddy_status_changed_cb), NULL);
	purple_signal_connect(blist_handle, "buddy-idle-changed", plugin,
	                    PURPLE_CALLBACK(buddy_idle_changed_cb), NULL);
	purple_signal_connect(blist_handle, "buddy-signed-on", plugin,
	                    PURPLE_CALLBACK(buddy_signon_cb), NULL);
	purple_signal_connect(blist_handle, "buddy-signed-off", plugin,
	                    PURPLE_CALLBACK(buddy_signoff_cb), NULL);

	purple_debug_info("statelog", "statelog plugin loaded.\n");

	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	purple_debug_info("statelog", "statelog plugin unloaded.\n");

	g_hash_table_destroy(buddy_states);

	return TRUE;
}

static PurplePluginUiInfo prefs_info =
{
	get_plugin_pref_frame,
	0,   /* page_num (Reserved) */
	NULL /* frame (Reserved) */
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

	STATELOG_PLUGIN_ID,                                 /**< id             */
	N_("State Logger"),                              /**< name           */
	DISPLAY_VERSION,                                  /**< version        */
	                                                  /**  summary        */
	N_("Logs presence changes and away messages of buddies."),
	                                                  /**  description    */
	N_("Logs presence changes and away messages of buddies.  Based on statenotify."),
	"Dylan Simon <dylan@dylex.net>",        /**< author         */
	NULL,                                     /**< homepage       */

	plugin_load,                                      /**< load           */
	plugin_unload,                                    /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	&prefs_info,
	NULL
};

static void
init_plugin(PurplePlugin *plugin)
{
	purple_prefs_add_none("/plugins/core/statelog");
	purple_prefs_add_bool("/plugins/core/statelog/to_conv", TRUE);
	purple_prefs_add_bool("/plugins/core/statelog/to_acct", FALSE);
	purple_prefs_add_bool("/plugins/core/statelog/to_self", FALSE);
	purple_prefs_add_bool("/plugins/core/statelog/log_away", TRUE);
	purple_prefs_add_bool("/plugins/core/statelog/log_idle", FALSE);
	purple_prefs_add_bool("/plugins/core/statelog/log_conn", TRUE);
	purple_prefs_add_bool("/plugins/core/statelog/log_msg", FALSE);
	purple_prefs_add_bool("/plugins/core/statelog/collapsed", FALSE);
}

PURPLE_INIT_PLUGIN(statelog, init_plugin, info)
