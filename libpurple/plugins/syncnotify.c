#include "internal.h"
#include "debug.h"
#include "plugin.h"
#include "version.h"
#include "blist.h"

/** Plugin id : type-author-name (to guarantee uniqueness) */
#define NOTIFY_PLUGIN_ID "core-dylex-syncnotify"

#define PATH "/home/dylan/.sync-notify"
static const struct sockaddr_un ADDR = {
	AF_UNIX,
	"/home/dylan/.sync-notify"
};

static void
conversation_created_cb(PurpleConversation *conv, void *data)
{
	if (conv->logs == NULL)
		return;
	PurpleLog *log = conv->logs->data;
	if (log == NULL)
		return;
	int s = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (s < 0)
	{
		purple_debug_warning("syncnotify", "socket: %m\n");
		return;
	}
	char *dir = purple_log_get_log_dir(log->type, log->name, log->account);
	const struct tm *tm = localtime(&log->time);
	const char *tz = purple_escape_filename(purple_utf8_strftime("%Z", tm));
	const char *date = purple_utf8_strftime("%Y-%m-%d.%H%M%S%z", tm);
	char *filename = g_strdup_printf("%s%s.html", date, tz);
	const char *path = g_build_filename(dir, filename, NULL);
	g_free(dir);
	g_free(filename);
	ssize_t r = sendto(s, path, strlen(path), 0, &ADDR, SUN_LEN(&ADDR)+1);
	if (r < 0 && errno != ENOENT && errno != ECONNREFUSED)
		purple_debug_warning("syncnotify", "sendto: %m\n");
	close(s);
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	purple_signal_connect(purple_conversations_get_handle(), "conversation-created", plugin,
	                    PURPLE_CALLBACK(conversation_created_cb), NULL);

	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
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

	NOTIFY_PLUGIN_ID,                                 /**< id             */
	N_("Dylex Notifier"),                              /**< name           */
	DISPLAY_VERSION,                                  /**< version        */
	                                                  /**  summary        */
	N_("Pushes relevant event notifications."),
	                                                  /**  description    */
	N_("Pushes relevant event notifications."),
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
}

PURPLE_INIT_PLUGIN(notify, init_plugin, info)
