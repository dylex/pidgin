
/*
 * gaim
 *
 * Some code copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 * libfaim code copyright 1998, 1999 Adam Fritzler <afritz@auk.cx>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "multi.h"
#include "prpl.h"
#include "gaim.h"
#include "aim.h"
#include "proxy.h"

#include "pixmaps/ab.xpm"
#include "pixmaps/admin_icon.xpm"
#include "pixmaps/aol_icon.xpm"
#include "pixmaps/away_icon.xpm"
#include "pixmaps/dt_icon.xpm"
#include "pixmaps/free_icon.xpm"
#include "pixmaps/wireless_icon.xpm"

#include "pixmaps/gnomeicu-online.xpm"
#include "pixmaps/gnomeicu-offline.xpm"
#include "pixmaps/gnomeicu-away.xpm"
#include "pixmaps/gnomeicu-dnd.xpm"
#include "pixmaps/gnomeicu-na.xpm"
#include "pixmaps/gnomeicu-occ.xpm"
#include "pixmaps/gnomeicu-ffc.xpm"

/* constants to identify proto_opts */
#define USEROPT_AUTH      0
#define USEROPT_AUTHPORT  1

#define UC_AOL		0x02
#define UC_ADMIN	0x04
#define UC_UNCONFIRMED	0x08
#define UC_NORMAL	0x10
#define UC_AB		0x20
#define UC_WIRELESS	0x40

#define AIMHASHDATA "http://gaim.sourceforge.net/aim_data.php3"

static int caps_aim = AIM_CAPS_CHAT | AIM_CAPS_BUDDYICON | AIM_CAPS_IMIMAGE;
/* What does AIM_CAPS_ICQ actually mean? -SE */
/*   static int caps_icq = AIM_CAPS_ICQ; */

/* Set AIM caps, because Gaim can still do them over ICQ and 
 * Winicq doesn't mind. */
static int caps_icq = AIM_CAPS_CHAT | AIM_CAPS_BUDDYICON | AIM_CAPS_IMIMAGE;

static fu8_t gaim_features[] = {0x01, 0x01, 0x01, 0x02};

struct oscar_data {
	aim_session_t *sess;
	aim_conn_t *conn;

	guint cnpa;
	guint paspa;

	GSList *create_rooms;

	gboolean conf;
	gboolean reqemail;
	gboolean setemail;
	char *email;
	gboolean setnick;
	char *newsn;
	gboolean chpass;
	char *oldp;
	char *newp;

	GSList *oscar_chats;
	GSList *direct_ims;
	GSList *hasicons;

	gboolean killme;
	gboolean icq;
	GSList *evilhack;

	struct {
		guint maxbuddies; /* max users you can watch */
		guint maxwatchers; /* max users who can watch you */
		guint maxpermits; /* max users on permit list */
		guint maxdenies; /* max users on deny list */
		guint maxsiglen; /* max size (bytes) of profile */
		guint maxawaymsglen; /* max size (bytes) of posted away message */
	} rights;
};

struct create_room {
	char *name;
	int exchange;
};

struct chat_connection {
	char *name;
	char *show; /* AOL did something funny to us */
	fu16_t exchange;
	fu16_t instance;
	int fd; /* this is redundant since we have the conn below */
	aim_conn_t *conn;
	int inpa;
	int id;
	struct gaim_connection *gc; /* i hate this. */
	struct conversation *cnv; /* bah. */
	int maxlen;
	int maxvis;
};

struct direct_im {
	struct gaim_connection *gc;
	char name[80];
	int watcher;
	aim_conn_t *conn;
	gboolean connected;
};

struct ask_direct {
	struct gaim_connection *gc;
	char *sn;
	char ip[64];
	fu8_t cookie[8];
};

struct icon_req {
	char *user;
	time_t timestamp;
	unsigned long length;
	unsigned long checksum;
	gboolean request;
};

struct channel4_data {
	struct gaim_connection *gc;
	gchar *uin;
	gchar *nick;
};

static struct direct_im *find_direct_im(struct oscar_data *od, const char *who) {
	GSList *d = od->direct_ims;
	char *n = g_strdup(normalize(who));
	struct direct_im *m = NULL;

	while (d) {
		m = (struct direct_im *)d->data;
		if (!strcmp(n, normalize(m->name)))
			break;
		m = NULL;
		d = d->next;
	}

	g_free(n);
	return m;
}

static char *extract_name(const char *name) {
	char *tmp;
	int i, j;
	char *x = strchr(name, '-');
	if (!x) return NULL;
	x = strchr(++x, '-');
	if (!x) return NULL;
	tmp = g_strdup(++x);

	for (i = 0, j = 0; x[i]; i++) {
		char hex[3];
		if (x[i] != '%') {
			tmp[j++] = x[i];
			continue;
		}
		strncpy(hex, x + ++i, 2); hex[2] = 0;
		i++;
		tmp[j++] = strtol(hex, NULL, 16);
	}

	tmp[j] = 0;
	return tmp;
}

static struct chat_connection *find_oscar_chat(struct gaim_connection *gc, int id) {
	GSList *g = ((struct oscar_data *)gc->proto_data)->oscar_chats;
	struct chat_connection *c = NULL;

	while (g) {
		c = (struct chat_connection *)g->data;
		if (c->id == id)
			break;
		g = g->next;
		c = NULL;
	}

	return c;
}

static struct chat_connection *find_oscar_chat_by_conn(struct gaim_connection *gc,
							aim_conn_t *conn) {
	GSList *g = ((struct oscar_data *)gc->proto_data)->oscar_chats;
	struct chat_connection *c = NULL;

	while (g) {
		c = (struct chat_connection *)g->data;
		if (c->conn == conn)
			break;
		g = g->next;
		c = NULL;
	}

	return c;
}

static int gaim_parse_auth_resp  (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_login      (aim_session_t *, aim_frame_t *, ...);
static int gaim_handle_redirect  (aim_session_t *, aim_frame_t *, ...);
static int gaim_info_change      (aim_session_t *, aim_frame_t *, ...);
static int gaim_account_confirm  (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_oncoming   (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_offgoing   (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_incoming_im(aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_misses     (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_clientauto (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_user_info  (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_motd       (aim_session_t *, aim_frame_t *, ...);
static int gaim_chatnav_info     (aim_session_t *, aim_frame_t *, ...);
static int gaim_chat_join        (aim_session_t *, aim_frame_t *, ...);
static int gaim_chat_leave       (aim_session_t *, aim_frame_t *, ...);
static int gaim_chat_info_update (aim_session_t *, aim_frame_t *, ...);
static int gaim_chat_incoming_msg(aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_msgack     (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_ratechange (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_evilnotify (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_searcherror(aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_searchreply(aim_session_t *, aim_frame_t *, ...);
static int gaim_bosrights        (aim_session_t *, aim_frame_t *, ...);
static int conninitdone_bos      (aim_session_t *, aim_frame_t *, ...);
static int conninitdone_admin    (aim_session_t *, aim_frame_t *, ...);
static int conninitdone_chat     (aim_session_t *, aim_frame_t *, ...);
static int conninitdone_chatnav  (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_msgerr     (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_locaterights(aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_buddyrights(aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_locerr     (aim_session_t *, aim_frame_t *, ...);
static int gaim_icbm_param_info  (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_genericerr (aim_session_t *, aim_frame_t *, ...);
static int gaim_memrequest       (aim_session_t *, aim_frame_t *, ...);
static int gaim_selfinfo         (aim_session_t *, aim_frame_t *, ...);
static int gaim_offlinemsg       (aim_session_t *, aim_frame_t *, ...);
static int gaim_offlinemsgdone   (aim_session_t *, aim_frame_t *, ...);
static int gaim_simpleinfo       (aim_session_t *, aim_frame_t *, ...);
static int gaim_popup            (aim_session_t *, aim_frame_t *, ...);
static int gaim_ssi_parserights  (aim_session_t *, aim_frame_t *, ...);
static int gaim_ssi_parselist    (aim_session_t *, aim_frame_t *, ...);

static int gaim_directim_initiate(aim_session_t *, aim_frame_t *, ...);
static int gaim_directim_incoming(aim_session_t *, aim_frame_t *, ...);
static int gaim_directim_typing  (aim_session_t *, aim_frame_t *, ...);
static int gaim_update_ui       (aim_session_t *, aim_frame_t *, ...);

static char *msgerrreason[] = {
	"Invalid error",
	"Invalid SNAC",
	"Rate to host",
	"Rate to client",
	"Not logged in",
	"Service unavailable",
	"Service not defined",
	"Obsolete SNAC",
	"Not supported by host",
	"Not supported by client",
	"Refused by client",
	"Reply too big",
	"Responses lost",
	"Request denied",
	"Busted SNAC payload",
	"Insufficient rights",
	"In local permit/deny",
	"Too evil (sender)",
	"Too evil (receiver)",
	"User temporarily unavailable",
	"No match",
	"List overflow",
	"Request ambiguous",
	"Queue full",
	"Not while on AOL"
};
static int msgerrreasonlen = 25;

static void gaim_directim_disconnect(aim_session_t *sess, aim_conn_t *conn) {
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	struct conversation *cnv;
	struct direct_im *dim;
	char *sn;
	char buf[256];

	sn = g_strdup(aim_directim_getsn(conn));

	debug_printf("%s disconnected Direct IM.\n", sn);

	dim = find_direct_im(od, sn);
	od->direct_ims = g_slist_remove(od->direct_ims, dim);
	gaim_input_remove(dim->watcher);

	if (dim->connected)
		g_snprintf(buf, sizeof buf, _("Direct IM with %s closed"), sn);
	else 
		g_snprintf(buf, sizeof buf, _("Direct IM with %s failed"), sn);
		
	if ((cnv = find_conversation(sn)))
		write_to_conv(cnv, buf, WFLAG_SYSTEM, NULL, time(NULL), -1);
	update_progress(cnv, 100);

	g_free(dim); /* I guess? I don't see it anywhere else... -- mid */
	g_free(sn);

	return;
}

static void oscar_callback(gpointer data, gint source,
				GaimInputCondition condition) {
	aim_conn_t *conn = (aim_conn_t *)data;
	aim_session_t *sess = aim_conn_getsess(conn);
	struct gaim_connection *gc = sess ? sess->aux_data : NULL;
	struct oscar_data *odata;

	if (!gc) {
		/* gc is null. we return, else we seg SIGSEG on next line. */
		debug_printf("oscar callback for closed connection (1).\n");
		return;
	}
      
	odata = (struct oscar_data *)gc->proto_data;

	if (!g_slist_find(connections, gc)) {
		/* oh boy. this is probably bad. i guess the only thing we 
		 * can really do is return? */
		debug_printf("oscar callback for closed connection (2).\n");
		return;
	}

	if (condition & GAIM_INPUT_READ) {
		if (conn->type == AIM_CONN_TYPE_RENDEZVOUS_OUT) {
			debug_printf("got information on rendezvous\n");
			if (aim_handlerendconnect(odata->sess, conn) < 0) {
				debug_printf(_("connection error (rend)\n"));
				aim_conn_kill(odata->sess, &conn);
			}
		} else {
			if (aim_get_command(odata->sess, conn) >= 0) {
				aim_rxdispatch(odata->sess);
                                if (odata->killme)
                                        signoff(gc);
			} else {
				if ((conn->type == AIM_CONN_TYPE_BOS) ||
					   !(aim_getconn_type(odata->sess, AIM_CONN_TYPE_BOS))) {
					debug_printf(_("major connection error\n"));
					hide_login_progress_error(gc, _("Disconnected."));
					signoff(gc);
				} else if (conn->type == AIM_CONN_TYPE_CHAT) {
					struct chat_connection *c = find_oscar_chat_by_conn(gc, conn);
					char buf[BUF_LONG];
					debug_printf("disconnected from chat room %s\n", c->name);
					c->conn = NULL;
					if (c->inpa > 0)
						gaim_input_remove(c->inpa);
					c->inpa = 0;
					c->fd = -1;
					aim_conn_kill(odata->sess, &conn);
					sprintf(buf, _("You have been disconnected from chat room %s."), c->name);
					do_error_dialog(buf, NULL, GAIM_ERROR);
				} else if (conn->type == AIM_CONN_TYPE_CHATNAV) {
					if (odata->cnpa > 0)
						gaim_input_remove(odata->cnpa);
					odata->cnpa = 0;
					debug_printf("removing chatnav input watcher\n");
					while (odata->create_rooms) {
						struct create_room *cr = odata->create_rooms->data;
						g_free(cr->name);
						odata->create_rooms =
							g_slist_remove(odata->create_rooms, cr);
						g_free(cr);
						do_error_dialog(_("Chat is currently unavailable"), NULL, GAIM_ERROR);
					}
					aim_conn_kill(odata->sess, &conn);
				} else if (conn->type == AIM_CONN_TYPE_AUTH) {
					if (odata->paspa > 0)
						gaim_input_remove(odata->paspa);
					odata->paspa = 0;
					debug_printf("removing authconn input watcher\n");
					aim_conn_kill(odata->sess, &conn);
				} else if (conn->type == AIM_CONN_TYPE_RENDEZVOUS) {
					if (conn->subtype == AIM_CONN_SUBTYPE_OFT_DIRECTIM)
						gaim_directim_disconnect(odata->sess, conn);
					else {
						debug_printf("No handler for rendezvous disconnect (%d).\n",
								source);
					}
					aim_conn_kill(odata->sess, &conn);
				} else {
					debug_printf("holy crap! generic connection error! %d\n",
							conn->type);
					aim_conn_kill(odata->sess, &conn);
				}
			}
		}
	}
}

static void oscar_debug(aim_session_t *sess, int level, const char *format, va_list va) {
	char *s = g_strdup_vprintf(format, va);
	char buf[256];
	char *t;
	struct gaim_connection *gc = sess->aux_data;

	g_snprintf(buf, sizeof(buf), "%s %d: ", gc->username, level);
	t = g_strconcat(buf, s, NULL);
	debug_printf(t);
	if (t[strlen(t)-1] != '\n')
		debug_printf("\n");
	g_free(t);
	g_free(s);
}

static void oscar_login_connect(gpointer data, gint source, GaimInputCondition cond)
{
	struct gaim_connection *gc = data;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *conn;

	if (!g_slist_find(connections, gc)) {
		close(source);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	conn = aim_getconn_type_all(sess, AIM_CONN_TYPE_AUTH);

	if (source < 0) {
		hide_login_progress(gc, _("Couldn't connect to host"));
		signoff(gc);
		return;
	}

	aim_conn_completeconnect(sess, conn);
	gc->inpa = gaim_input_add(conn->fd, GAIM_INPUT_READ,
			oscar_callback, conn);
	debug_printf(_("Password sent, waiting for response\n"));
}

static void oscar_login(struct aim_user *user) {
	aim_session_t *sess;
	aim_conn_t *conn;
	char buf[256];
	struct gaim_connection *gc = new_gaim_conn(user);
	struct oscar_data *odata = gc->proto_data = g_new0(struct oscar_data, 1);

	if (isdigit(*user->username)) {
		odata->icq = TRUE;
		/* this is odd but it's necessary for a proper do_import and do_export */
		gc->protocol = PROTO_ICQ;
		gc->password[8] = 0;
	} else {
		gc->protocol = PROTO_TOC;
		gc->flags |= OPT_CONN_HTML;
	}

	sess = g_new0(aim_session_t, 1);

	aim_session_init(sess, AIM_SESS_FLAGS_NONBLOCKCONNECT, 0);
	aim_setdebuggingcb(sess, oscar_debug);

	/* we need an immediate queue because we don't use a while-loop to
	 * see if things need to be sent. */
	aim_tx_setenqueue(sess, AIM_TX_IMMEDIATE, NULL);
	odata->sess = sess;
	sess->aux_data = gc;

	conn = aim_newconn(sess, AIM_CONN_TYPE_AUTH, NULL);
	if (conn == NULL) {
		debug_printf(_("internal connection error\n"));
		hide_login_progress(gc, _("Unable to login to AIM"));
		signoff(gc);
		return;
	}

	g_snprintf(buf, sizeof(buf), _("Signon: %s"), gc->username);
	set_login_progress(gc, 2, buf);

	aim_conn_addhandler(sess, conn, 0x0017, 0x0007, gaim_parse_login, 0);
	aim_conn_addhandler(sess, conn, 0x0017, 0x0003, gaim_parse_auth_resp, 0);

	conn->status |= AIM_CONN_STATUS_INPROGRESS;
	conn->fd = proxy_connect(user->proto_opt[USEROPT_AUTH][0] ?
					user->proto_opt[USEROPT_AUTH] : FAIM_LOGIN_SERVER,
				 user->proto_opt[USEROPT_AUTHPORT][0] ?
					atoi(user->proto_opt[USEROPT_AUTHPORT]) : FAIM_LOGIN_PORT,
				 oscar_login_connect, gc);
	if (conn->fd < 0) {
		hide_login_progress(gc, _("Couldn't connect to host"));
		signoff(gc);
		return;
	}
	aim_request_login(sess, conn, gc->username);
}

static void oscar_close(struct gaim_connection *gc) {
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;
	
	while (odata->oscar_chats) {
		struct chat_connection *n = odata->oscar_chats->data;
		if (n->inpa > 0)
			gaim_input_remove(n->inpa);
		g_free(n->name);
		g_free(n->show);
		odata->oscar_chats = g_slist_remove(odata->oscar_chats, n);
		g_free(n);
	}
	while (odata->direct_ims) {
		struct direct_im *n = odata->direct_ims->data;
		if (n->watcher > 0)
			gaim_input_remove(n->watcher);
		odata->direct_ims = g_slist_remove(odata->direct_ims, n);
		g_free(n);
	}
	while (odata->hasicons) {
		struct icon_req *n = odata->hasicons->data;
		g_free(n->user);
		odata->hasicons = g_slist_remove(odata->hasicons, n);
		g_free(n);
	}
	while (odata->evilhack) {
		g_free(odata->evilhack->data);
		odata->evilhack = g_slist_remove(odata->evilhack, odata->evilhack->data);
	}
	while (odata->create_rooms) {
		struct create_room *cr = odata->create_rooms->data;
		g_free(cr->name);
		odata->create_rooms = g_slist_remove(odata->create_rooms, cr);
		g_free(cr);
	}
	if (odata->email)
		g_free(odata->email);
	if (odata->newp)
		g_free(odata->newp);
	if (odata->oldp)
		g_free(odata->oldp);
	if (gc->inpa > 0)
		gaim_input_remove(gc->inpa);
	if (odata->cnpa > 0)
		gaim_input_remove(odata->cnpa);
	if (odata->paspa > 0)
		gaim_input_remove(odata->paspa);
	aim_session_kill(odata->sess);
	g_free(odata->sess);
	odata->sess = NULL;
	g_free(gc->proto_data);
	gc->proto_data = NULL;
	debug_printf(_("Signed off.\n"));
}

static void oscar_bos_connect(gpointer data, gint source, GaimInputCondition cond) {
	struct gaim_connection *gc = data;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *bosconn;

	if (!g_slist_find(connections, gc)) {
		close(source);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	bosconn = odata->conn;

	if (source < 0) {
		hide_login_progress(gc, _("Could Not Connect"));
		signoff(gc);
		return;
	}

	aim_conn_completeconnect(sess, bosconn);
	gc->inpa = gaim_input_add(bosconn->fd, GAIM_INPUT_READ,
			oscar_callback, bosconn);
	set_login_progress(gc, 4, _("Connection established, cookie sent"));
}

static int gaim_parse_auth_resp(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	struct aim_authresp_info *info;
	int i; char *host; int port;
	struct aim_user *user;
	aim_conn_t *bosconn;

	struct gaim_connection *gc = sess->aux_data;
        struct oscar_data *od = gc->proto_data;
	user = gc->user;
	port = user->proto_opt[USEROPT_AUTHPORT][0] ?
		atoi(user->proto_opt[USEROPT_AUTHPORT]) : FAIM_LOGIN_PORT,

	va_start(ap, fr);
	info = va_arg(ap, struct aim_authresp_info *);
	va_end(ap);

	debug_printf("inside auth_resp (Screen name: %s)\n", info->sn);

	if (info->errorcode || !info->bosip || !info->cookie) {
		switch (info->errorcode) {
		case 0x05:
			/* Incorrect nick/password */
			hide_login_progress(gc, _("Incorrect nickname or password."));
			plugin_event(event_error, (void *)980, 0, 0, 0);
			break;
		case 0x11:
			/* Suspended account */
			hide_login_progress(gc, _("Your account is currently suspended."));
			break;
		case 0x18:
			/* connecting too frequently */
			hide_login_progress(gc, _("You have been connecting and disconnecting too frequently. Wait ten minutes and try again. If you continue to try, you will need to wait even longer."));
			plugin_event(event_error, (void *)983, 0, 0, 0);
			break;
		case 0x1c:
			/* client too old */
			hide_login_progress(gc, _("The client version you are using is too old. Please upgrade at " WEBSITE));
			plugin_event(event_error, (void *)989, 0, 0, 0);
			break;
		default:
			hide_login_progress(gc, _("Authentication Failed"));
			break;
		}
		debug_printf("Login Error Code 0x%04x\n", info->errorcode);
		debug_printf("Error URL: %s\n", info->errorurl);
		od->killme = TRUE;
		return 1;
	}


	debug_printf("Reg status: %d\n", info->regstatus);
	if (info->email) {
		debug_printf("Email: %s\n", info->email);
	} else {
		debug_printf("Email is NULL\n");
	}
	debug_printf("BOSIP: %s\n", info->bosip);
	debug_printf("Closing auth connection...\n");
	aim_conn_kill(sess, &fr->conn);

	bosconn = aim_newconn(sess, AIM_CONN_TYPE_BOS, NULL);
	if (bosconn == NULL) {
		hide_login_progress(gc, _("Internal Error"));
		od->killme = TRUE;
		return 0;
	}

	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNINITDONE, conninitdone_bos, 0);
	aim_conn_addhandler(sess, bosconn, 0x0009, 0x0003, gaim_bosrights, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_ACK, AIM_CB_ACK_ACK, NULL, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, AIM_CB_GEN_REDIRECT, gaim_handle_redirect, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_LOC, AIM_CB_LOC_RIGHTSINFO, gaim_parse_locaterights, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_BUD, AIM_CB_BUD_RIGHTSINFO, gaim_parse_buddyrights, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_BUD, AIM_CB_BUD_ONCOMING, gaim_parse_oncoming, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_BUD, AIM_CB_BUD_OFFGOING, gaim_parse_offgoing, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_INCOMING, gaim_parse_incoming_im, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_LOC, AIM_CB_LOC_ERROR, gaim_parse_locerr, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_MISSEDCALL, gaim_parse_misses, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_CLIENTAUTORESP, gaim_parse_clientauto, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, AIM_CB_GEN_RATECHANGE, gaim_parse_ratechange, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, AIM_CB_GEN_EVIL, gaim_parse_evilnotify, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_LOK, AIM_CB_LOK_ERROR, gaim_parse_searcherror, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_LOK, 0x0003, gaim_parse_searchreply, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_ERROR, gaim_parse_msgerr, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_LOC, AIM_CB_LOC_USERINFO, gaim_parse_user_info, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_ACK, gaim_parse_msgack, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, AIM_CB_GEN_MOTD, gaim_parse_motd, 0);
	aim_conn_addhandler(sess, bosconn, 0x0004, 0x0005, gaim_icbm_param_info, 0);
	aim_conn_addhandler(sess, bosconn, 0x0001, 0x0001, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, bosconn, 0x0003, 0x0001, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, bosconn, 0x0009, 0x0001, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, bosconn, 0x0001, 0x001f, gaim_memrequest, 0);
	aim_conn_addhandler(sess, bosconn, 0x0001, 0x000f, gaim_selfinfo, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_OFFLINEMSG, gaim_offlinemsg, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_OFFLINEMSGCOMPLETE, gaim_offlinemsgdone, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_POP, 0x0002, gaim_popup, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_SIMPLEINFO, gaim_simpleinfo, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_SSI, AIM_CB_SSI_RIGHTSINFO, gaim_ssi_parserights, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_SSI, AIM_CB_SSI_LIST, gaim_ssi_parselist, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_SSI, AIM_CB_SSI_NOLIST, gaim_ssi_parselist, 0);

	((struct oscar_data *)gc->proto_data)->conn = bosconn;
	for (i = 0; i < (int)strlen(info->bosip); i++) {
		if (info->bosip[i] == ':') {
			port = atoi(&(info->bosip[i+1]));
			break;
		}
	}
	host = g_strndup(info->bosip, i);
	bosconn->status |= AIM_CONN_STATUS_INPROGRESS;
	bosconn->fd = proxy_connect(host, port, oscar_bos_connect, gc);
	g_free(host);
	if (bosconn->fd < 0) {
		hide_login_progress(gc, _("Could Not Connect"));
		od->killme = TRUE;
		return 0;
	}
	aim_sendcookie(sess, bosconn, info->cookie);
	gaim_input_remove(gc->inpa);

	return 1;
}

struct pieceofcrap {
	struct gaim_connection *gc;
	unsigned long offset;
	unsigned long len;
	char *modname;
	int fd;
	aim_conn_t *conn;
	unsigned int inpa;
};

static void damn_you(gpointer data, gint source, GaimInputCondition c)
{
	struct pieceofcrap *pos = data;
	struct oscar_data *od = pos->gc->proto_data;
	char in = '\0';
	int x = 0;
	unsigned char m[17];

	while (read(pos->fd, &in, 1) == 1) {
		if (in == '\n')
			x++;
		else if (in != '\r')
			x = 0;
		if (x == 2)
			break;
		in = '\0';
	}
	if (in != '\n') {
		do_error_dialog(_("Gaim was Unable to get a valid AIM login hash."),
				_("You may be disconnected shortly.  You may want to use TOC until "
				  "this is fixed.  Check " WEBSITE " for updates."), GAIM_WARNING);
		gaim_input_remove(pos->inpa);
		close(pos->fd);
		g_free(pos);
		return;
	}
	read(pos->fd, m, 16);
	m[16] = '\0';
	debug_printf("Sending hash: ");
	for (x = 0; x < 16; x++)
		debug_printf("%02x ", (unsigned char)m[x]);
	debug_printf("\n");
	gaim_input_remove(pos->inpa);
	close(pos->fd);
	aim_sendmemblock(od->sess, pos->conn, 0, 16, m, AIM_SENDMEMBLOCK_FLAG_ISHASH);
	g_free(pos);
}

static void straight_to_hell(gpointer data, gint source, GaimInputCondition cond) {
	struct pieceofcrap *pos = data;
	char buf[BUF_LONG];

	if (source < 0) {
		do_error_dialog(_("Gaim was Unable to get a valid AIM login hash."),
				_("You may be disconnected shortly.  You may want to use TOC until "
				  "this is fixed.  Check " WEBSITE " for updates."), GAIM_WARNING);
		if (pos->modname)
			g_free(pos->modname);
		g_free(pos);
		return;
	}

	g_snprintf(buf, sizeof(buf), "GET " AIMHASHDATA
			"?offset=%ld&len=%ld&modname=%s HTTP/1.0\n\n",
			pos->offset, pos->len, pos->modname ? pos->modname : "");
	write(pos->fd, buf, strlen(buf));
	if (pos->modname)
		g_free(pos->modname);
	pos->inpa = gaim_input_add(pos->fd, GAIM_INPUT_READ, damn_you, pos);
	return;
}

/* size of icbmui.ocm, the largest module in AIM 3.5 */
#define AIM_MAX_FILE_SIZE 98304

int gaim_memrequest(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	struct pieceofcrap *pos;
	fu32_t offset, len;
	char *modname;
	int fd;

	va_start(ap, fr);
	offset = (fu32_t)va_arg(ap, unsigned long);
	len = (fu32_t)va_arg(ap, unsigned long);
	modname = va_arg(ap, char *);
	va_end(ap);

	debug_printf("offset: %d, len: %d, file: %s\n", offset, len, modname ? modname : "aim.exe");
	if (len == 0) {
		debug_printf("len is 0, hashing NULL\n");
		aim_sendmemblock(sess, fr->conn, offset, len, NULL,
				AIM_SENDMEMBLOCK_FLAG_ISREQUEST);
		return 1;
	}
	/* uncomment this when you're convinced it's right. remember, it's been wrong before.
	if (offset > AIM_MAX_FILE_SIZE || len > AIM_MAX_FILE_SIZE) {
		char *buf;
		int i = 8;
		if (modname)
			i += strlen(modname);
		buf = g_malloc(i);
		i = 0;
		if (modname) {
			memcpy(buf, modname, strlen(modname));
			i += strlen(modname);
		}
		buf[i++] = offset & 0xff;
		buf[i++] = (offset >> 8) & 0xff;
		buf[i++] = (offset >> 16) & 0xff;
		buf[i++] = (offset >> 24) & 0xff;
		buf[i++] = len & 0xff;
		buf[i++] = (len >> 8) & 0xff;
		buf[i++] = (len >> 16) & 0xff;
		buf[i++] = (len >> 24) & 0xff;
		debug_printf("len + offset is invalid, hashing request\n");
		aim_sendmemblock(sess, command->conn, offset, i, buf, AIM_SENDMEMBLOCK_FLAG_ISREQUEST);
		g_free(buf);
		return 1;
	}
	*/

	pos = g_new0(struct pieceofcrap, 1);
	pos->gc = sess->aux_data;
	pos->conn = fr->conn;

	pos->offset = offset;
	pos->len = len;
	pos->modname = modname ? g_strdup(modname) : NULL;

	fd = proxy_connect("gaim.sourceforge.net", 80, straight_to_hell, pos);
	if (fd < 0) {
		if (pos->modname)
			g_free(pos->modname);
		g_free(pos);
		do_error_dialog(_("Gaim was Unable to get valid login hash."),
				_("You may be disconnected shortly.  You may want to use TOC until "
				  "this is fixed.  Check " WEBSITE " for updates."), GAIM_WARNING);
	}
	pos->fd = fd;

	return 1;
}

static int gaim_parse_login(aim_session_t *sess, aim_frame_t *fr, ...) {
	char *key;
	va_list ap;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = gc->proto_data;

	va_start(ap, fr);
	key = va_arg(ap, char *);
	va_end(ap);

	if (odata->icq) {
		struct client_info_s info = CLIENTINFO_ICQ_KNOWNGOOD;
		aim_send_login(sess, fr->conn, gc->username, gc->password, &info, key);
	} else {
#if 0
		struct client_info_s info = {"gaim", 4, 1, 2010, "us", "en", 0x0004, 0x0000, 0x04b};
#endif
		struct client_info_s info = CLIENTINFO_AIM_KNOWNGOOD;
		aim_send_login(sess, fr->conn, gc->username, gc->password, &info, key);
	}

	return 1;
}

static int conninitdone_chat(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	struct chat_connection *chatcon;
	static int id = 1;

	aim_conn_addhandler(sess, fr->conn, 0x000e, 0x0001, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CHT, AIM_CB_CHT_USERJOIN, gaim_chat_join, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CHT, AIM_CB_CHT_USERLEAVE, gaim_chat_leave, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CHT, AIM_CB_CHT_ROOMINFOUPDATE, gaim_chat_info_update, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CHT, AIM_CB_CHT_INCOMINGMSG, gaim_chat_incoming_msg, 0);

	aim_clientready(sess, fr->conn);

	chatcon = find_oscar_chat_by_conn(gc, fr->conn);
	chatcon->id = id;
	chatcon->cnv = serv_got_joined_chat(gc, id++, chatcon->show);

	return 1;
}

static int conninitdone_chatnav(aim_session_t *sess, aim_frame_t *fr, ...) {

	aim_conn_addhandler(sess, fr->conn, 0x000d, 0x0001, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CTN, AIM_CB_CTN_INFO, gaim_chatnav_info, 0);

	aim_clientready(sess, fr->conn);

	aim_chatnav_reqrights(sess, fr->conn);

	return 1;
}

static void oscar_chatnav_connect(gpointer data, gint source, GaimInputCondition cond) {
	struct gaim_connection *gc = data;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *tstconn;

	if (!g_slist_find(connections, gc)) {
		close(source);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	tstconn = aim_getconn_type_all(sess, AIM_CONN_TYPE_CHATNAV);

	if (source < 0) {
		aim_conn_kill(sess, &tstconn);
		debug_printf("unable to connect to chatnav server\n");
		return;
	}

	aim_conn_completeconnect(sess, tstconn);
	odata->cnpa = gaim_input_add(tstconn->fd, GAIM_INPUT_READ,
					oscar_callback, tstconn);
	debug_printf("chatnav: connected\n");
}

static void oscar_auth_connect(gpointer data, gint source, GaimInputCondition cond)
{
	struct gaim_connection *gc = data;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *tstconn;

	if (!g_slist_find(connections, gc)) {
		close(source);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	tstconn = aim_getconn_type_all(sess, AIM_CONN_TYPE_AUTH);

	if (source < 0) {
		aim_conn_kill(sess, &tstconn);
		debug_printf("unable to connect to authorizer\n");
		return;
	}

	aim_conn_completeconnect(sess, tstconn);
	odata->paspa = gaim_input_add(tstconn->fd, GAIM_INPUT_READ,
				oscar_callback, tstconn);
	debug_printf("chatnav: connected\n");
}

static void oscar_chat_connect(gpointer data, gint source, GaimInputCondition cond)
{
	struct chat_connection *ccon = data;
	struct gaim_connection *gc = ccon->gc;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *tstconn;

	if (!g_slist_find(connections, gc)) {
		close(source);
		g_free(ccon->show);
		g_free(ccon->name);
		g_free(ccon);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	tstconn = ccon->conn;

	if (source < 0) {
		aim_conn_kill(sess, &tstconn);
		g_free(ccon->show);
		g_free(ccon->name);
		g_free(ccon);
		return;
	}

	aim_conn_completeconnect(sess, ccon->conn);
	ccon->inpa = gaim_input_add(tstconn->fd,
			GAIM_INPUT_READ,
			oscar_callback, tstconn);
	odata->oscar_chats = g_slist_append(odata->oscar_chats, ccon);
}

/* Hrmph. I don't know how to make this look better. --mid */
static int gaim_handle_redirect(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	struct aim_redirect_data *redir;
	struct gaim_connection *gc = sess->aux_data;
	struct aim_user *user = gc->user;
	aim_conn_t *tstconn;
	int i;
	char *host;
	int port;

	port = user->proto_opt[USEROPT_AUTHPORT][0] ?
		atoi(user->proto_opt[USEROPT_AUTHPORT]) : FAIM_LOGIN_PORT,

	va_start(ap, fr);
	redir = va_arg(ap, struct aim_redirect_data *);
	va_end(ap);

	for (i = 0; i < (int)strlen(redir->ip); i++) {
		if (redir->ip[i] == ':') {
			port = atoi(&(redir->ip[i+1]));
			break;
		}
	}
	host = g_strndup(redir->ip, i);

	switch(redir->group) {
	case 0x7: /* Authorizer */
		debug_printf("Reconnecting with authorizor...\n");
		tstconn = aim_newconn(sess, AIM_CONN_TYPE_AUTH, NULL);
		if (tstconn == NULL) {
			debug_printf("unable to reconnect with authorizer\n");
			g_free(host);
			return 1;
		}
		aim_conn_addhandler(sess, tstconn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNINITDONE, conninitdone_admin, 0);
		aim_conn_addhandler(sess, tstconn, 0x0007, 0x0003, gaim_info_change, 0);
		aim_conn_addhandler(sess, tstconn, 0x0007, 0x0005, gaim_info_change, 0);
		aim_conn_addhandler(sess, tstconn, 0x0007, 0x0007, gaim_account_confirm, 0);

		tstconn->status |= AIM_CONN_STATUS_INPROGRESS;
		tstconn->fd = proxy_connect(host, port, oscar_auth_connect, gc);
		if (tstconn->fd < 0) {
			aim_conn_kill(sess, &tstconn);
			debug_printf("unable to reconnect with authorizer\n");
			g_free(host);
			return 1;
		}
		aim_sendcookie(sess, tstconn, redir->cookie);
		break;
	case 0xd: /* ChatNav */
		tstconn = aim_newconn(sess, AIM_CONN_TYPE_CHATNAV, NULL);
		if (tstconn == NULL) {
			debug_printf("unable to connect to chatnav server\n");
			g_free(host);
			return 1;
		}
		aim_conn_addhandler(sess, tstconn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNINITDONE, conninitdone_chatnav, 0);

		tstconn->status |= AIM_CONN_STATUS_INPROGRESS;
		tstconn->fd = proxy_connect(host, port, oscar_chatnav_connect, gc);
		if (tstconn->fd < 0) {
			aim_conn_kill(sess, &tstconn);
			debug_printf("unable to connect to chatnav server\n");
			g_free(host);
			return 1;
		}
		aim_sendcookie(sess, tstconn, redir->cookie);
		break;
	case 0xe: /* Chat */
		{
		struct chat_connection *ccon;

		tstconn = aim_newconn(sess, AIM_CONN_TYPE_CHAT, NULL);
		if (tstconn == NULL) {
			debug_printf("unable to connect to chat server\n");
			g_free(host);
			return 1;
		}

		aim_conn_addhandler(sess, tstconn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNINITDONE, conninitdone_chat, 0);

		ccon = g_new0(struct chat_connection, 1);
		ccon->conn = tstconn;
		ccon->gc = gc;
		ccon->fd = -1;
		ccon->name = g_strdup(redir->chat.room);
		ccon->exchange = redir->chat.exchange;
		ccon->instance = redir->chat.instance;
		ccon->show = extract_name(redir->chat.room);
		
		ccon->conn->status |= AIM_CONN_STATUS_INPROGRESS;
		ccon->conn->fd = proxy_connect(host, port, oscar_chat_connect, ccon);
		if (ccon->conn->fd < 0) {
			aim_conn_kill(sess, &tstconn);
			debug_printf("unable to connect to chat server\n");
			g_free(host);
			g_free(ccon->show);
			g_free(ccon->name);
			g_free(ccon);
			return 1;
		}
		aim_sendcookie(sess, tstconn, redir->cookie);
		debug_printf("Connected to chat room %s exchange %d\n", ccon->name, ccon->exchange);
		}
		break;
	default: /* huh? */
		debug_printf("got redirect for unknown service 0x%04x\n", redir->group);
		break;
	}

	g_free(host);
	return 1;
}

static int gaim_parse_oncoming(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *od = gc->proto_data;
	aim_userinfo_t *info;
	time_t time_idle = 0, signon = 0;
	int type = 0;
	int caps = 0;
	char *tmp;

	va_list ap;
	va_start(ap, fr);
	info = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	if (info->present & AIM_USERINFO_PRESENT_CAPABILITIES)
		caps = info->capabilities;
	if (info->flags & AIM_FLAG_ACTIVEBUDDY)
		type |= UC_AB;

	if ((!od->icq) && (info->present & AIM_USERINFO_PRESENT_FLAGS)) {
			if (info->flags & AIM_FLAG_UNCONFIRMED)
				type |= UC_UNCONFIRMED;
			if (info->flags & AIM_FLAG_ADMINISTRATOR)
				type |= UC_ADMIN;
			if (info->flags & AIM_FLAG_AOL)
				type |= UC_AOL;
			if (info->flags & AIM_FLAG_FREE)
				type |= UC_NORMAL;
			if (info->flags & AIM_FLAG_AWAY)
				type |= UC_UNAVAILABLE;
			if (info->flags & AIM_FLAG_WIRELESS)
				type |= UC_WIRELESS;
	}
	if (info->present & AIM_USERINFO_PRESENT_ICQEXTSTATUS) {
		type = (info->icqinfo.status << 7);
		if (!(info->icqinfo.status & AIM_ICQ_STATE_CHAT) &&
		      (info->icqinfo.status != AIM_ICQ_STATE_NORMAL)) {
			type |= UC_UNAVAILABLE;
		}
	}

	if (caps & AIM_CAPS_ICQ)
		caps ^= AIM_CAPS_ICQ;

	if (info->present & AIM_USERINFO_PRESENT_IDLE) {
		time(&time_idle);
		time_idle -= info->idletime*60;
	}

	if (info->present & AIM_USERINFO_PRESENT_SESSIONLEN)
		signon = time(NULL) - info->sessionlen;

	tmp = g_strdup(normalize(gc->username));
	if (!strcmp(tmp, normalize(info->sn)))
		g_snprintf(gc->displayname, sizeof(gc->displayname), "%s", info->sn);
	g_free(tmp);

	serv_got_update(gc, info->sn, 1, info->warnlevel/10, signon,
			time_idle, type, caps);

	return 1;
}

static int gaim_parse_offgoing(aim_session_t *sess, aim_frame_t *fr, ...) {
	aim_userinfo_t *info;
	va_list ap;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	info = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	serv_got_update(gc, info->sn, 0, 0, 0, 0, 0, 0);

	return 1;
}

static void cancel_direct_im(gpointer w, struct ask_direct *d) {
	debug_printf("Freeing DirectIM prompts.\n");

	g_free(d->sn);
	g_free(d);
}

static void oscar_directim_callback(gpointer data, gint source, GaimInputCondition condition) {
	struct direct_im *dim = data;
	struct gaim_connection *gc = dim->gc;
	struct oscar_data *od = gc->proto_data;
	struct conversation *cnv;
	char buf[256];
	struct sockaddr name;
	socklen_t name_len = 1;
	
	if (!g_slist_find(connections, gc)) {
		g_free(dim);
		return;
	}

	if (source < 0) {
		g_free(dim);
		return;
	}

	if (dim->conn->fd != source)
		dim->conn->fd = source;
	aim_conn_completeconnect(od->sess, dim->conn);
	if (!(cnv = find_conversation(dim->name))) 
		cnv = new_conversation(dim->name);

	/* This is the best way to see if we're connected or not */
	if (getpeername(source, &name, &name_len) == 0) {
		g_snprintf(buf, sizeof buf, _("Direct IM with %s established"), dim->name);
		dim->connected = TRUE;
		write_to_conv(cnv, buf, WFLAG_SYSTEM, NULL, time(NULL), -1);
	}
	od->direct_ims = g_slist_append(od->direct_ims, dim);
	
	dim->watcher = gaim_input_add(dim->conn->fd, GAIM_INPUT_READ,
				      oscar_callback, dim->conn);
}

static int accept_direct_im(gpointer w, struct ask_direct *d) {
	struct gaim_connection *gc = d->gc;
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	struct direct_im *dim;
	char *host; int port = 4443;
	int i;

	debug_printf("Accepted DirectIM.\n");

	dim = find_direct_im(od, d->sn);
	if (dim) {
		cancel_direct_im(w, d); /* 40 */
		return TRUE;
	}
	dim = g_new0(struct direct_im, 1);
	dim->gc = d->gc;
	g_snprintf(dim->name, sizeof dim->name, "%s", d->sn);

	dim->conn = aim_directim_connect(od->sess, d->sn, NULL, d->cookie);
	if (!dim->conn) {
		g_free(dim);
		cancel_direct_im(w, d);
		return TRUE;
	}

	aim_conn_addhandler(od->sess, dim->conn, AIM_CB_FAM_OFT, AIM_CB_OFT_DIRECTIMINCOMING,
				gaim_directim_incoming, 0);
	aim_conn_addhandler(od->sess, dim->conn, AIM_CB_FAM_OFT, AIM_CB_OFT_DIRECTIMTYPING,
				gaim_directim_typing, 0);
	aim_conn_addhandler(od->sess, dim->conn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_IMAGETRANSFER,
			        gaim_update_ui, 0);
	for (i = 0; i < (int)strlen(d->ip); i++) {
		if (d->ip[i] == ':') {
			port = atoi(&(d->ip[i+1]));
			break;
		}
	}
	host = g_strndup(d->ip, i);
	dim->conn->status |= AIM_CONN_STATUS_INPROGRESS;
	dim->conn->fd = proxy_connect(host, port, oscar_directim_callback, dim);
	g_free(host);
	if (dim->conn->fd < 0) {
		aim_conn_kill(od->sess, &dim->conn);
		g_free(dim);
		cancel_direct_im(w, d);
		return TRUE;
	}

	cancel_direct_im(w, d);

	return TRUE;
}

static int incomingim_chan1(aim_session_t *sess, aim_conn_t *conn, aim_userinfo_t *userinfo, struct aim_incomingim_ch1_args *args) {
	char *tmp = g_malloc(BUF_LONG);
	struct gaim_connection *gc = sess->aux_data;
	int flags = 0;

	if (args->icbmflags & AIM_IMFLAGS_AWAY)
		flags |= IM_FLAG_AWAY;

	if (args->icbmflags & AIM_IMFLAGS_HASICON) {
		struct oscar_data *od = gc->proto_data;
		struct icon_req *ir = NULL;
		GSList *h = od->hasicons;
		char *who = normalize(userinfo->sn);
		debug_printf("%s has an icon\n", userinfo->sn);
		while (h) {
			ir = h->data;
			if (!strcmp(ir->user, who))
				break;
			h = h->next;
		}
		if (!h) {
			ir = g_new0(struct icon_req, 1);
			ir->user = g_strdup(who);
			od->hasicons = g_slist_append(od->hasicons, ir);
		}
		if ((args->iconlen != ir->length) ||
		    (args->iconsum != ir->checksum) ||
		    (args->iconstamp != ir->timestamp))
			ir->request = TRUE;
		ir->length = args->iconlen;
		ir->checksum = args->iconsum;
		ir->timestamp = args->iconstamp;
	}

	if (gc->user->iconfile[0] && (args->icbmflags & AIM_IMFLAGS_BUDDYREQ)) {
		FILE *file;
		struct stat st;

		if (!stat(gc->user->iconfile, &st)) {
			char *buf = g_malloc(st.st_size);
			file = fopen(gc->user->iconfile, "r");
			if (file) {
				int len = fread(buf, 1, st.st_size, file);
				debug_printf("Sending buddy icon to %s (%d bytes, %d reported)\n",
						userinfo->sn, len, st.st_size);
				aim_send_icon(sess, userinfo->sn, buf, st.st_size,
					      st.st_mtime, aim_iconsum(buf, st.st_size));
				fclose(file);
			} else
				debug_printf("Can't open buddy icon file!\n");
			g_free(buf);
		} else
			debug_printf("Can't stat buddy icon file!\n");
	}

	/*
	 * Quickly convert it to eight bit format, replacing 
	 * non-ASCII UNICODE characters with their equivelent 
	 * HTML entity.
	 */
	if (args->icbmflags & AIM_IMFLAGS_UNICODE) {
		int i;
		
		for (i = 0, tmp[0] = '\0'; i < args->msglen; i += 2) {
			unsigned short uni;
			
			uni = ((args->msg[i] & 0xff) << 8) | (args->msg[i+1] & 0xff);

			if ((uni < 128) || ((uni >= 160) && (uni <= 255))) { /* ISO 8859-1 */
				
				g_snprintf(tmp+strlen(tmp), BUF_LONG-strlen(tmp), "%c", uni);
				
			} else { /* something else, do UNICODE entity */
				g_snprintf(tmp+strlen(tmp), BUF_LONG-strlen(tmp), "&#%04x;", uni);
			}
		}
	} else
		g_snprintf(tmp, BUF_LONG, "%s", args->msg);

	strip_linefeed(tmp);
	serv_got_im(gc, userinfo->sn, tmp, flags, time(NULL), -1);
	g_free(tmp);

	return 1;
}

static int incomingim_chan2(aim_session_t *sess, aim_conn_t *conn, aim_userinfo_t *userinfo, struct aim_incomingim_ch2_args *args) {
	struct gaim_connection *gc = sess->aux_data;

	debug_printf("rendezvous status %d (%s)\n", args->status, userinfo->sn);

	if (args->status != AIM_RENDEZVOUS_PROPOSE)
		return 1;

	if (args->reqclass & AIM_CAPS_CHAT) {
		char *name = extract_name(args->info.chat.roominfo.name);
		int *exch = g_new0(int, 1);
		GList *m = NULL;
		m = g_list_append(m, g_strdup(name ? name : args->info.chat.roominfo.name));
		*exch = args->info.chat.roominfo.exchange;
		m = g_list_append(m, exch);
		serv_got_chat_invite(gc,
				     name ? name : args->info.chat.roominfo.name,
				     userinfo->sn,
				     (char *)args->msg,
				     m);
		if (name)
			g_free(name);
	} else if (args->reqclass & AIM_CAPS_SENDFILE) {
	} else if (args->reqclass & AIM_CAPS_GETFILE) {
	} else if (args->reqclass & AIM_CAPS_VOICE) {
	} else if (args->reqclass & AIM_CAPS_BUDDYICON) {
		set_icon_data(gc, normalize(userinfo->sn), args->info.icon.icon,
				args->info.icon.length);
	} else if (args->reqclass & AIM_CAPS_IMIMAGE) {
		struct ask_direct *d = g_new0(struct ask_direct, 1);
		char buf[256];

		debug_printf("%s received direct im request from %s (%s)\n",
				gc->username, userinfo->sn, args->verifiedip);

		d->gc = gc;
		d->sn = g_strdup(userinfo->sn);
		strncpy(d->ip, args->verifiedip, sizeof(d->ip));
		memcpy(d->cookie, args->cookie, 8);
		g_snprintf(buf, sizeof buf, "%s has just asked to directly connect to %s.",
				userinfo->sn, gc->username);
		do_ask_dialog(buf, d, accept_direct_im, cancel_direct_im);
	} else {
		debug_printf("Unknown reqclass %d\n", args->reqclass);
	}

	return 1;
}

/*
 * Next 2 functions are for when other people ask you for authorization
 */
static void gaim_icq_authgrant(gpointer w, struct channel4_data *data) {
	char message;
	struct oscar_data *od = (struct oscar_data *)data->gc->proto_data;
	message = 0;
	aim_send_im_ch4(od->sess, data->uin, AIM_ICQMSG_AUTHGRANTED, &message);
	show_got_added(data->gc, NULL, data->uin, NULL, NULL);
	g_free(data->uin);
	data->uin = NULL;
}

static void gaim_icq_authdeny(gpointer w, struct channel4_data *data) {
	if (data->uin) {
		gchar *message;
		struct oscar_data *od = (struct oscar_data *)data->gc->proto_data;
		message = g_strdup_printf("No reason given.");
		aim_send_im_ch4(od->sess, data->uin, AIM_ICQMSG_AUTHDENIED, message);
		g_free(data->uin);
		g_free(message);
	}
	g_free(data);
}

/*
 * For when other people ask you for authorization
 */
static void gaim_icq_authask(struct gaim_connection *gc, fu32_t uin, char *msg) {
	struct channel4_data *data = g_new(struct channel4_data, 1);
	/* The first 6 chars of the message are some type of alien gibberish, so skip them */
	char *dialog_msg = g_strdup_printf("The user %lu wants to add you to their buddy list for the following reason:\n\n%s", uin, (msg && strlen(msg)>6) ? msg+6 : "No reason given.");
	debug_printf("Received an authorization request from UIN %lu\n", uin);
	data->gc = gc;
	data->uin = g_strdup_printf("%lu", uin);
	do_ask_dialog(dialog_msg, data, gaim_icq_authgrant, gaim_icq_authdeny);
	g_free(dialog_msg);
}

/*
 * Next 2 functions are for when someone sends you contacts
 */
static void gaim_icq_contactadd(gpointer w, struct channel4_data *data) {
	show_add_buddy(data->gc, data->uin, NULL, data->nick);
}

static void gaim_icq_contactdontadd(gpointer w, struct channel4_data *data) {
	free(data->uin);
	free(data->nick);
	g_free(data);
}

static int incomingim_chan4(aim_session_t *sess, aim_conn_t *conn, aim_userinfo_t *userinfo, struct aim_incomingim_ch4_args *args) {
	struct gaim_connection *gc = sess->aux_data;

	switch (args->type) 
		{
		case 0x0001: { /* An almost-normal instant message.  Mac ICQ sends this.  It's peculiar. */
			gchar *uin, *message;
			uin = g_strdup_printf("%lu", args->uin);
			message = g_strdup(args->msg);
			strip_linefeed(message);
			serv_got_im(gc, uin, message, 0, time(NULL), -1);
			g_free(uin);
			g_free(message);
		} break;

		case 0x0004: { /* Someone sent you a URL */
			gchar **text, *uin, *message;
			text = g_strsplit(args->msg, "�", 2);
			if (text) {
				uin = g_strdup_printf("%lu", args->uin);
				message = g_strdup_printf("<A HREF=\"%s\">%s</A>", text[1], text[0]);
				strip_linefeed(message);
				serv_got_im(gc, uin, message, 0, time(NULL), -1);
				g_strfreev(text);
				g_free(uin);
				g_free(message);
			}
		} break;

		case 0x0006: { /* Someone requested authorization */
			gaim_icq_authask(gc, args->uin, args->msg);
		} break;

		case 0x0007: { /* Someone has denied you authorization */
			char *dialog_msg;
			dialog_msg = g_strdup_printf(_("The user %lu has denied your request to add them to your contact list for the following reason:\n%s"), args->uin, args->msg ? args->msg : _("No reason given."));
			do_error_dialog(_("ICQ authorization denied."), dialog_msg, GAIM_ERROR);
			g_free(dialog_msg);
		} break;

		case 0x0008: { /* Someone has granted you authorization */
			char *dialog_msg;
			dialog_msg = g_strdup_printf(_("The user %lu has granted your request to add them to your contact list."), args->uin);
			do_error_dialog("ICQ authorization accepted.", dialog_msg, GAIM_INFO);
			g_free(dialog_msg);
		} break;

		case 0x0012: {
			/* Ack for authorizing/denying someone.  Or possibly an ack for sending any system notice */
		} break;

		case 0x0013: { /* Someone has sent you some ICQ contacts */
			int i, num;
			gchar **text;
			text = g_strsplit(args->msg, "�", 0);
			if (text) {
				num = 0;
				for (i=0; i<strlen(text[0]); i++)
					num = num*10 + text[0][i]-48;
				for (i=0; i<num; i++) {
					struct channel4_data *data = g_new(struct channel4_data, 1);
					gchar *message = g_strdup_printf("ICQ user %lu has sent you a contact: %s (%s)\nDo you want to add this contact to your contact list?", args->uin, text[i*2+2], text[i*2+1]);
					data->gc = gc;
					data->uin = g_strdup(text[i*2+2]);
					data->nick = g_strdup(text[i*2+1]);
					do_ask_dialog(message, data, gaim_icq_contactadd, gaim_icq_contactdontadd);
					g_free(message);
				}
				g_strfreev(text);
			}
		} break;

		case 0x001a: { /* Someone has requested that you send them from ICQ contacts */
			/* This is boring and silly. */
		} break;

		default: {
			debug_printf("Received a channel 4 message of unknown type (type 0x%04d).\n", args->type);
		} break;
	}

	return 1;
}

static int gaim_parse_incoming_im(aim_session_t *sess, aim_frame_t *fr, ...) {
	int channel, ret = 0;
	aim_userinfo_t *userinfo;
	va_list ap;

	va_start(ap, fr);
	channel = va_arg(ap, int);
	userinfo = va_arg(ap, aim_userinfo_t *);

	switch (channel) {
		case 1: { /* standard message */
			struct aim_incomingim_ch1_args *args;
			args = va_arg(ap, struct aim_incomingim_ch1_args *);
			ret = incomingim_chan1(sess, fr->conn, userinfo, args);
		} break;

		case 2: { /* rendevous */
			struct aim_incomingim_ch2_args *args;
			args = va_arg(ap, struct aim_incomingim_ch2_args *);
			ret = incomingim_chan2(sess, fr->conn, userinfo, args);
		} break;

		case 4: { /* ICQ */
			struct aim_incomingim_ch4_args *args;
			args = va_arg(ap, struct aim_incomingim_ch4_args *);
			ret = incomingim_chan4(sess, fr->conn, userinfo, args);
		} break;

		default: {
			debug_printf("ICBM received on unsupported channel (channel 0x%04d).", channel);
		} break;
	}

	va_end(ap);

	return ret;
}

static int gaim_parse_misses(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	fu16_t chan, nummissed, reason;
	aim_userinfo_t *userinfo;
	char buf[1024];

	va_start(ap, fr);
	chan = (fu16_t)va_arg(ap, unsigned int);
	userinfo = va_arg(ap, aim_userinfo_t *);
	nummissed = (fu16_t)va_arg(ap, unsigned int);
	reason = (fu16_t)va_arg(ap, unsigned int);
	va_end(ap);

	switch(reason) {
		case 0:
			/* Invalid (0) */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ? 
				   _("You missed %d message from %s because it was invalid.") :
				   _("You missed %d messages from %s because they were invalid."),
				   nummissed,
				   userinfo->sn);
			break;
		case 1:
			/* Message too large */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ?
				   _("You missed %d message from %s because it was too large.") :
				   _("You missed %d messages from %s because they were too large."),
				   nummissed,
				   userinfo->sn);
			break;
		case 2:
			/* Rate exceeded */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ? 
				   _("You missed %d message from %s because the rate limit has been exceeded.") :
				   _("You missed %d messages from %s because the rate limit has been exceeded."),
				   nummissed,
				   userinfo->sn);
			break;
		case 3:
			/* Evil Sender */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ?
				   _("You missed %d message from %s because it was too evil.") : 
				   _("You missed %d messages from %s because they are too evil."),
				   nummissed,
				   userinfo->sn);
			break;
		case 4:
			/* Evil Receiver */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ? 
				   _("You missed %d message from %s because you are too evil.") :
				   _("You missed %d messages from %s because you are too evil."),
				   nummissed,
				   userinfo->sn);
			break;
		default:
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ? 
				   _("You missed %d message from %s for unknown reasons.") :
				   _("You missed %d messages from %s for unknown reasons."),
				   nummissed,
				   userinfo->sn);
			break;
	}
	do_error_dialog(buf, NULL, GAIM_ERROR);

	return 1;
}

static char *gaim_icq_status(int state) {
	/* Make a cute little string that shows the status of the dude or dudet */
	if (state & AIM_ICQ_STATE_CHAT)
		return g_strdup_printf("Free For Chat");
	else if (state & AIM_ICQ_STATE_DND)
		return g_strdup_printf("Do Not Disturb");
	else if (state & AIM_ICQ_STATE_OUT)
		return g_strdup_printf("Not Available");
	else if (state & AIM_ICQ_STATE_BUSY)
		return g_strdup_printf("Occupied");
	else if (state & AIM_ICQ_STATE_AWAY)
		return g_strdup_printf("Away");
	else if (state & AIM_ICQ_STATE_WEBAWARE)
		return g_strdup_printf("Web Aware");
	else if (state & AIM_ICQ_STATE_INVISIBLE)
		return g_strdup_printf("Invisible");
	else
		return g_strdup_printf("Online");
}

static int gaim_parse_clientauto(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	va_list ap;
	fu16_t chan, reason;
	char *who;

	va_start(ap, fr);
	chan = (fu16_t)va_arg(ap, unsigned int);
	who = va_arg(ap, char *);
	reason = (fu16_t)va_arg(ap, unsigned int);

	switch(reason) {
		case 0x0003: { /* Reply from an ICQ status message request */
			int state = (int)va_arg(ap, fu32_t);
			char *msg = va_arg(ap, char *);
			char *status_msg = gaim_icq_status(state);
			char *dialog_msg, **splitmsg;
			struct oscar_data *od = gc->proto_data;
			GSList *l = od->evilhack;
			gboolean evilhack = FALSE;

			/* Split at (carriage return/newline)'s, then rejoin later with BRs between. */
			splitmsg = g_strsplit(msg, "\r\n", 0);

			/* If who is in od->evilhack, then we're just getting the away message, otherwise this 
			 * will just get appended to the info box (which is already showing). */
			while (l) {
				char *x = l->data;
				if (!strcmp(x, normalize(who))) {
					evilhack = TRUE;
					g_free(x);
					od->evilhack = g_slist_remove(od->evilhack, x);
					break;
				}
				l = l->next;
			}

			if (evilhack)
				dialog_msg = g_strdup_printf(_("<B>UIN:</B> %s<BR><B>Status:</B> %s<BR><HR><BR>%s<BR>"), who, status_msg, g_strjoinv("<BR>", splitmsg));
			else
				dialog_msg = g_strdup_printf(_("<B>Status:</B> %s<BR><HR><BR>%s<BR>"), status_msg, g_strjoinv("<BR>", splitmsg));
			g_show_info_text(gc, who, 2, dialog_msg, NULL);

			g_free(status_msg);
			g_free(dialog_msg);
			g_strfreev(splitmsg);
		} break;

		default: {
			debug_printf("Received an unknown client auto-response from %s.  Type 0x%04x\n", who, reason);
		} break;
	}
	va_end(ap);

	return 1;
}

static int gaim_parse_genericerr(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	fu16_t reason;
	char *m;

	va_start(ap, fr);
	reason = (fu16_t)va_arg(ap, unsigned int);
	va_end(ap);

	debug_printf("snac threw error (reason 0x%04x: %s)\n", reason,
			(reason < msgerrreasonlen) ? msgerrreason[reason] : "unknown");

	m = g_strdup_printf(_("SNAC threw error: %s\n"),
			reason < msgerrreasonlen ? msgerrreason[reason] : "Unknown error");
	do_error_dialog(m, NULL, GAIM_ERROR);
	g_free(m);

	return 1;
}

static int gaim_parse_msgerr(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	char *destn;
	fu16_t reason;
	char buf[1024];

	va_start(ap, fr);
	reason = (fu16_t)va_arg(ap, unsigned int);
	destn = va_arg(ap, char *);
	va_end(ap);

	sprintf(buf, _("Your message to %s did not get sent: %s"), destn);
	do_error_dialog(buf, (reason < msgerrreasonlen) ? msgerrreason[reason] : _("No reason was given."), GAIM_ERROR);

	return 1;
}

static int gaim_parse_locerr(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	char *destn;
	fu16_t reason;
	char buf[1024];

	va_start(ap, fr);
	reason = (fu16_t)va_arg(ap, unsigned int);
	destn = va_arg(ap, char *);
	va_end(ap);

	sprintf(buf, _("User information for %s unavailable: %s"), destn);
	do_error_dialog(buf, (reason < msgerrreasonlen) ? msgerrreason[reason] : _("No reason was given."), GAIM_ERROR);

	return 1;
}

static char *images(int flags) {
	static char buf[1024];
	g_snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s",
			(flags & AIM_FLAG_ACTIVEBUDDY) ? "<IMG SRC=\"ab_icon.gif\">" : "",
			(flags & AIM_FLAG_UNCONFIRMED) ? "<IMG SRC=\"dt_icon.gif\">" : "",
			(flags & AIM_FLAG_AOL) ? "<IMG SRC=\"aol_icon.gif\">" : "",
			(flags & AIM_FLAG_ICQ) ? "<IMG SRC=\"icq_icon.gif\">" : "",
			(flags & AIM_FLAG_ADMINISTRATOR) ? "<IMG SRC=\"admin_icon.gif\">" : "",
			(flags & AIM_FLAG_FREE) ? "<IMG SRC=\"free_icon.gif\">" : "",
			(flags & AIM_FLAG_WIRELESS) ? "<IMG SRC=\"wireless_icon.gif\">" : "");
	return buf;
}


/* XXX This is horribly copied from ../../buddy.c. */
static char *caps_string(guint caps)
{
	static char buf[512], *tmp;
	int count = 0, i = 0;
	guint bit = 1;
	while (bit <= 0x10000) {
		if (bit & caps) {
			switch (bit) {
			case 0x1:
				tmp = _("Buddy Icon");
				break;
			case 0x2:
				tmp = _("Voice");
				break;
			case 0x4:
				tmp = _("IM Image");
				break;
			case 0x8:
				tmp = _("Chat");
				break;
			case 0x10:
				tmp = _("Get File");
				break;
			case 0x20:
				tmp = _("Send File");
				break;
			case 0x40:
			case 0x200:
				tmp = _("Games");
				break;
			case 0x80:
				tmp = _("Stocks");
				break;
			case 0x100:
				tmp = _("Send Buddy List");
				break;
			case 0x400:
				tmp = _("EveryBuddy Bug");
				break;
			case 0x800:
				tmp = _("AP User");
				break;
			case 0x1000:
				tmp = _("ICQ RTF");
				break;
			case 0x2000:
				tmp = _("Nihilist");
				break;
			case 0x4000:
				tmp = _("ICQ Server Relay");
				break;
			case 0x8000:
				tmp = _("ICQ Unknown");
				break;
			case 0x10000:
				tmp = _("Trillian Encryption");
				break;
			default:
				tmp = NULL;
				break;
			}
			if (tmp)
				i += g_snprintf(buf + i, sizeof(buf) - i, "%s%s", (count ? ", " : ""),
						tmp);
			count++;
		}
		bit <<= 1;
	}
	return buf;
}

static int gaim_parse_user_info(aim_session_t *sess, aim_frame_t *fr, ...) {
	aim_userinfo_t *info;
	char *prof_enc = NULL, *prof = NULL;
	fu16_t infotype;
	char header[BUF_LONG];
	char legend[BUF_LONG];
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *od = gc->proto_data;
	GSList *l = od->evilhack;
	gboolean evilhack = FALSE;
	va_list ap;
	gchar *membersince = NULL, *onlinesince = NULL, *idle = NULL;

	va_start(ap, fr);
	info = va_arg(ap, aim_userinfo_t *);
	infotype = (fu16_t)va_arg(ap, unsigned int);
	prof_enc = va_arg(ap, char *);
	prof = va_arg(ap, char *);
	va_end(ap);

	if (!od->icq) {
		g_snprintf(legend, sizeof legend,
				_("<br><BODY BGCOLOR=WHITE><hr><I>Legend:</I><br><br>"
				"<IMG SRC=\"free_icon.gif\"> : Normal AIM User<br>"
				"<IMG SRC=\"aol_icon.gif\"> : AOL User <br>"
				"<IMG SRC=\"dt_icon.gif\"> : Trial AIM User <br>"
				"<IMG SRC=\"admin_icon.gif\"> : Administrator <br>"
				"<IMG SRC=\"ab_icon.gif\"> : ActiveBuddy Interactive Agent<br>"
				"<IMG SRC=\"wireless_icon.gif\"> : Wireless Device User<br>"));
	} else {
		legend[0] = 0;
	}
		
	if (info->present & AIM_USERINFO_PRESENT_ONLINESINCE) {
		onlinesince = g_strdup_printf("Online Since : <B>%s</B><BR>\n",
					asctime(localtime(&info->onlinesince)));
	}

	if (info->present & AIM_USERINFO_PRESENT_MEMBERSINCE) {
		membersince = g_strdup_printf("Member Since : <B>%s</B><BR>\n",
					asctime(localtime(&info->membersince)));
	}

	if (info->present & AIM_USERINFO_PRESENT_IDLE) {
		idle = g_strdup_printf("Idle : <B>%d minutes</B>",
					info->idletime);
	} else
		idle = g_strdup("Idle: <B>Active</B>");

	g_snprintf(header, sizeof header,
			_("Username : <B>%s</B>  %s <BR>\n"
			"Warning Level : <B>%d %%</B><BR>\n"
			"%s"
			"%s"
			"%s<BR>\n"
			"<HR><BR>\n"),
			info->sn, images(info->flags),
			info->warnlevel/10,
			onlinesince ? onlinesince : "",
			membersince ? membersince : "",
			idle ? idle : "");

	g_free(onlinesince);
	g_free(membersince);
	g_free(idle);

	while (l) {
		char *x = l->data;
		if (!strcmp(x, normalize(info->sn))) {
			evilhack = TRUE;
			g_free(x);
			od->evilhack = g_slist_remove(od->evilhack, x);
			break;
		}
		l = l->next;
	}

	if (infotype == AIM_GETINFO_AWAYMESSAGE) {
		if (evilhack) {
			g_show_info_text(gc, info->sn, 2,
					 header,
					 (prof && *prof) ? away_subs(prof, gc->username) :
						_("<i>User has no away message</i>"),
					 legend, NULL);
		} else {
			g_show_info_text(gc, info->sn, 0,
					 header,
					 (prof && *prof) ? away_subs(prof, gc->username) : NULL,
					 (prof && *prof) ? "<BR><HR><BR>" : NULL,
					 NULL);
		}
	} else if (infotype == AIM_GETINFO_CAPABILITIES) {
		g_show_info_text(gc, info->sn, 2,
				header,
				"<i>", _("Client Capabilities: "),
				caps_string(info->capabilities),
				"</i>",
				legend,
				NULL);
	} else {
		g_show_info_text(gc, info->sn, 1,
				 (prof && *prof) ? away_subs(prof, gc->username) :
					_("<i>No Information Provided</i>"),
				 legend,
				 NULL);
	}

	return 1;
}

static int gaim_parse_motd(aim_session_t *sess, aim_frame_t *fr, ...) {
	char *msg;
	fu16_t id;
	va_list ap;
	char buildbuf[150];

	va_start(ap, fr);
	id  = (fu16_t)va_arg(ap, unsigned int);
	msg = va_arg(ap, char *);
	va_end(ap);

	aim_getbuildstring(buildbuf, sizeof(buildbuf));

	debug_printf("MOTD: %s (%d)\n", msg ? msg : "Unknown", id);
	if (id < 4)
		do_error_dialog(_("Your AIM connection may be lost."), NULL, GAIM_WARNING);

	return 1;
}

static int gaim_chatnav_info(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	fu16_t type;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;

	va_start(ap, fr);
	type = (fu16_t)va_arg(ap, unsigned int);

	switch(type) {
		case 0x0002: {
			fu8_t maxrooms;
			struct aim_chat_exchangeinfo *exchanges;
			int exchangecount, i;

			maxrooms = (fu8_t)va_arg(ap, unsigned int);
			exchangecount = va_arg(ap, int);
			exchanges = va_arg(ap, struct aim_chat_exchangeinfo *);
			va_end(ap);

			debug_printf("chat info: Chat Rights:\n");
			debug_printf("chat info: \tMax Concurrent Rooms: %d\n", maxrooms);
			debug_printf("chat info: \tExchange List: (%d total)\n", exchangecount);
			for (i = 0; i < exchangecount; i++)
				debug_printf("chat info: \t\t%d    %s\n", exchanges[i].number, exchanges[i].name ? exchanges[i].name : "");
			while (odata->create_rooms) {
				struct create_room *cr = odata->create_rooms->data;
				debug_printf("creating room %s\n", cr->name);
				aim_chatnav_createroom(sess, fr->conn, cr->name, cr->exchange);
				g_free(cr->name);
				odata->create_rooms = g_slist_remove(odata->create_rooms, cr);
				g_free(cr);
			}
			}
			break;
		case 0x0008: {
			char *fqcn, *name, *ck;
			fu16_t instance, flags, maxmsglen, maxoccupancy, unknown, exchange;
			fu8_t createperms;
			fu32_t createtime;

			fqcn = va_arg(ap, char *);
			instance = (fu16_t)va_arg(ap, unsigned int);
			exchange = (fu16_t)va_arg(ap, unsigned int);
			flags = (fu16_t)va_arg(ap, unsigned int);
			createtime = va_arg(ap, fu32_t);
			maxmsglen = (fu16_t)va_arg(ap, unsigned int);
			maxoccupancy = (fu16_t)va_arg(ap, unsigned int);
			createperms = (fu8_t)va_arg(ap, int);
			unknown = (fu16_t)va_arg(ap, unsigned int);
			name = va_arg(ap, char *);
			ck = va_arg(ap, char *);
			va_end(ap);

			debug_printf("created room: %s %d %d %d %lu %d %d %d %d %s %s\n",
					fqcn,
					exchange, instance, flags,
					createtime,
					maxmsglen, maxoccupancy, createperms, unknown,
					name, ck);
			aim_chat_join(odata->sess, odata->conn, exchange, ck, instance);
			}
			break;
		default:
			va_end(ap);
			debug_printf("chatnav info: unknown type (%04x)\n", type);
			break;
	}
	return 1;
}

static int gaim_chat_join(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	int count, i;
	aim_userinfo_t *info;
	struct gaim_connection *g = sess->aux_data;

	struct chat_connection *c = NULL;

	va_start(ap, fr);
	count = va_arg(ap, int);
	info  = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	c = find_oscar_chat_by_conn(g, fr->conn);
	if (!c)
		return 1;

	for (i = 0; i < count; i++)
		add_chat_buddy(c->cnv, info[i].sn);

	return 1;
}

static int gaim_chat_leave(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	int count, i;
	aim_userinfo_t *info;
	struct gaim_connection *g = sess->aux_data;

	struct chat_connection *c = NULL;

	va_start(ap, fr);
	count = va_arg(ap, int);
	info  = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	c = find_oscar_chat_by_conn(g, fr->conn);
	if (!c)
		return 1;

	for (i = 0; i < count; i++)
		remove_chat_buddy(c->cnv, info[i].sn, NULL);

	return 1;
}

static int gaim_chat_info_update(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	aim_userinfo_t *userinfo;
	struct aim_chat_roominfo *roominfo;
	char *roomname;
	int usercount;
	char *roomdesc;
	fu16_t unknown_c9, unknown_d2, unknown_d5, maxmsglen, maxvisiblemsglen;
	fu32_t creationtime;
	struct gaim_connection *gc = sess->aux_data;
	struct chat_connection *ccon = find_oscar_chat_by_conn(gc, fr->conn);

	va_start(ap, fr);
	roominfo = va_arg(ap, struct aim_chat_roominfo *);
	roomname = va_arg(ap, char *);
	usercount= va_arg(ap, int);
	userinfo = va_arg(ap, aim_userinfo_t *);
	roomdesc = va_arg(ap, char *);
	unknown_c9 = (fu16_t)va_arg(ap, int);
	creationtime = (fu32_t)va_arg(ap, unsigned long);
	maxmsglen = (fu16_t)va_arg(ap, int);
	unknown_d2 = (fu16_t)va_arg(ap, int);
	unknown_d5 = (fu16_t)va_arg(ap, int);
	maxvisiblemsglen = (fu16_t)va_arg(ap, int);
	va_end(ap);

	debug_printf("inside chat_info_update (maxmsglen = %d, maxvislen = %d)\n",
			maxmsglen, maxvisiblemsglen);

	ccon->maxlen = maxmsglen;
	ccon->maxvis = maxvisiblemsglen;

	return 1;
}

static int gaim_chat_incoming_msg(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	aim_userinfo_t *info;
	char *msg;
	struct gaim_connection *gc = sess->aux_data;
	struct chat_connection *ccon = find_oscar_chat_by_conn(gc, fr->conn);
	char *tmp;

	va_start(ap, fr);
	info = va_arg(ap, aim_userinfo_t *);
	msg  = va_arg(ap, char *);

	tmp = g_malloc(BUF_LONG);
	g_snprintf(tmp, BUF_LONG, "%s", msg);
	serv_got_chat_in(gc, ccon->id, info->sn, 0, tmp, time((time_t)NULL));
	g_free(tmp);

	return 1;
}

/*
 * Recieved in response to an IM sent with the AIM_IMFLAGS_ACK option.
 */
static int gaim_parse_msgack(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	fu16_t type;
	char *sn;

	va_start(ap, fr);
	type = (fu16_t)va_arg(ap, unsigned int);
	sn = va_arg(ap, char *);
	va_end(ap);

	debug_printf("Sent message to %s.\n", sn);

	return 1;
}

static int gaim_parse_ratechange(aim_session_t *sess, aim_frame_t *fr, ...) {
	static const char *codes[5] = {
		"invalid",
		 "change",
		 "warning",
		 "limit",
		 "limit cleared",
	};
	va_list ap;
	fu16_t code, rateclass;
	fu32_t windowsize, clear, alert, limit, disconnect, currentavg, maxavg;

	va_start(ap, fr); 
	code = (fu16_t)va_arg(ap, unsigned int);
	rateclass= (fu16_t)va_arg(ap, unsigned int);
	windowsize = (fu32_t)va_arg(ap, unsigned long);
	clear = (fu32_t)va_arg(ap, unsigned long);
	alert = (fu32_t)va_arg(ap, unsigned long);
	limit = (fu32_t)va_arg(ap, unsigned long);
	disconnect = (fu32_t)va_arg(ap, unsigned long);
	currentavg = (fu32_t)va_arg(ap, unsigned long);
	maxavg = (fu32_t)va_arg(ap, unsigned long);
	va_end(ap);

	debug_printf("rate %s (paramid 0x%04lx): curavg = %ld, maxavg = %ld, alert at %ld, "
		     "clear warning at %ld, limit at %ld, disconnect at %ld (window size = %ld)\n",
		     (code < 5) ? codes[code] : codes[0],
		     rateclass,
		     currentavg, maxavg,
		     alert, clear,
		     limit, disconnect,
		     windowsize);

	/* XXX fix these values */
	if (code == AIM_RATE_CODE_CHANGE) {
		if (currentavg >= clear)
			aim_conn_setlatency(fr->conn, 0);
	} else if (code == AIM_RATE_CODE_WARNING) {
		aim_conn_setlatency(fr->conn, windowsize/4);
	} else if (code == AIM_RATE_CODE_LIMIT) {
		do_error_dialog(_("Rate limiting error."),
				_("The last message was not sent because you are over the rate limit.  "
				  "Please wait 10 seconds and try again."), GAIM_ERROR);
		aim_conn_setlatency(fr->conn, windowsize/2);
	} else if (code == AIM_RATE_CODE_CLEARLIMIT) {
		aim_conn_setlatency(fr->conn, 0);
	}

	return 1;
}

static int gaim_parse_evilnotify(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	fu16_t newevil;
	aim_userinfo_t *userinfo;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	newevil = (fu16_t)va_arg(ap, unsigned int);
	userinfo = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	serv_got_eviled(gc, (userinfo && userinfo->sn[0]) ? userinfo->sn : NULL, newevil / 10);

	return 1;
}

static int gaim_selfinfo(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	aim_userinfo_t *info;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	info = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	gc->evil = info->warnlevel/10;
	/* gc->correction_time = (info->onlinesince - gc->login_time); */

	return 1;
}

static int conninitdone_bos(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *od = gc->proto_data;

	aim_reqpersonalinfo(sess, fr->conn);
	aim_bos_reqlocaterights(sess, fr->conn);
	aim_bos_reqbuddyrights(sess, fr->conn);

	aim_reqicbmparams(sess);

	aim_bos_reqrights(sess, fr->conn);
	if (od->icq) {
		aim_bos_setgroupperm(sess, fr->conn, AIM_FLAG_ALLUSERS);
		aim_bos_setprivacyflags(sess, fr->conn, AIM_PRIVFLAGS_ALLOWIDLE |
			AIM_PRIVFLAGS_ALLOWMEMBERSINCE);
	}

	return 1;
}

static int conninitdone_admin(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *od = gc->proto_data;

	aim_clientready(sess, fr->conn);
	debug_printf("connected to admin\n");

	if (od->chpass) {
		debug_printf("changing password\n");
		aim_admin_changepasswd(sess, fr->conn, od->newp, od->oldp);
		g_free(od->oldp);
		od->oldp = NULL;
		g_free(od->newp);
		od->newp = NULL;
		od->chpass = FALSE;
	}
	if (od->setnick) {
		debug_printf("formatting screenname\n");
		aim_admin_setnick(sess, fr->conn, od->newsn);
		g_free(od->newsn);
		od->newsn = NULL;
		od->setnick = FALSE;
	}
	if (od->conf) {
		debug_printf("confirming account\n");
		aim_admin_reqconfirm(sess, fr->conn);
		od->conf = FALSE;
	}
	if (od->reqemail) {
		debug_printf("requesting email\n");
		aim_admin_getinfo(sess, fr->conn, 0x0011);
		od->reqemail = FALSE;
	}
	if (od->setemail) {
		debug_printf("setting email\n");
		aim_admin_setemail(sess, fr->conn, od->email);
		g_free(od->email);
		od->setemail = FALSE;
	}

	return 1;
}

static int gaim_icbm_param_info(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct aim_icbmparameters *params;
	va_list ap;

	va_start(ap, fr);
	params = va_arg(ap, struct aim_icbmparameters *);
	va_end(ap);

	/* evidently this crashes on solaris. i have no clue why
	debug_printf("ICBM Parameters: maxchannel = %d, default flags = 0x%08lx, max msg len = %d, "
			"max sender evil = %f, max receiver evil = %f, min msg interval = %ld\n",
			params->maxchan, params->flags, params->maxmsglen,
			((float)params->maxsenderwarn)/10.0, ((float)params->maxrecverwarn)/10.0,
			params->minmsginterval);
	*/

	/* Maybe senderwarn and recverwarn should be user preferences... */
	params->maxmsglen = 8000;
	params->minmsginterval = 0;

	aim_seticbmparam(sess, params);

	return 1;
}

static int gaim_parse_locaterights(aim_session_t *sess, aim_frame_t *fr, ...)
{
	va_list ap;
	fu16_t maxsiglen;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;

	va_start(ap, fr);
	maxsiglen = va_arg(ap, int);
	va_end(ap);

	debug_printf("locate rights: max sig len = %d\n", maxsiglen);

	odata->rights.maxsiglen = odata->rights.maxawaymsglen = (guint)maxsiglen;

	if (odata->icq)
		aim_bos_setprofile(sess, fr->conn, NULL, NULL, caps_icq);
	else
		aim_bos_setprofile(sess, fr->conn, gc->user->user_info, NULL, caps_aim);

	return 1;
}

static int gaim_parse_buddyrights(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	fu16_t maxbuddies, maxwatchers;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;

	va_start(ap, fr);
	maxbuddies = (fu16_t)va_arg(ap, unsigned int);
	maxwatchers = (fu16_t)va_arg(ap, unsigned int);
	va_end(ap);

	debug_printf("buddy list rights: Max buddies = %d / Max watchers = %d\n", maxbuddies, maxwatchers);

	odata->rights.maxbuddies = (guint)maxbuddies;
	odata->rights.maxwatchers = (guint)maxwatchers;

	return 1;
}

static int gaim_bosrights(aim_session_t *sess, aim_frame_t *fr, ...) {
	fu16_t maxpermits, maxdenies;
	va_list ap;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;

	va_start(ap, fr);
	maxpermits = (fu16_t)va_arg(ap, unsigned int);
	maxdenies = (fu16_t)va_arg(ap, unsigned int);
	va_end(ap);

	debug_printf("BOS rights: Max permit = %d / Max deny = %d\n", maxpermits, maxdenies);

	odata->rights.maxpermits = (guint)maxpermits;
	odata->rights.maxdenies = (guint)maxdenies;

	account_online(gc);
	serv_finish_login(gc);

	if (bud_list_cache_exists(gc))
		do_import(gc, NULL);

	debug_printf("buddy list loaded\n");

	aim_clientready(sess, fr->conn);

	aim_icq_reqofflinemsgs(sess);

	aim_reqservice(sess, fr->conn, AIM_CONN_TYPE_CHATNAV);

	if (!odata->icq) {
		debug_printf("ssi: requesting ssi list\n");
		aim_ssi_reqrights(sess, fr->conn);
		aim_ssi_reqdata(sess, fr->conn, sess->ssi.timestamp, sess->ssi.revision);
	}

	return 1;
}

static int gaim_offlinemsg(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	struct aim_icq_offlinemsg *msg;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	msg = va_arg(ap, struct aim_icq_offlinemsg *);
	va_end(ap);

	debug_printf("Received offline message of type 0x%04x\n", msg->type);

	switch (msg->type) {
		case 0x0001: { /* Basic offline message */
			char sender[32];
			char *dialog_msg = g_strdup(msg->msg);
			time_t t = get_time(msg->year, msg->month, msg->day, msg->hour, msg->minute, 0);
			g_snprintf(sender, sizeof(sender), "%lu", msg->sender);
			strip_linefeed(dialog_msg);
			serv_got_im(gc, sender, dialog_msg, 0, t, -1);
			g_free(dialog_msg);
		} break;

		case 0x0006: { /* Authorization request */
			gaim_icq_authask(gc, msg->sender, msg->msg);
		} break;

		case 0x0007: { /* Someone has denied you authorization */
			char *dialog_msg;
			dialog_msg = g_strdup_printf(_("The user %lu has denied your request to add them to your contact list for the following reason:\n%s"), msg->sender, msg->msg ? msg->msg : _("No reason given."));
			do_error_dialog(_("ICQ Authorization denied"), dialog_msg, GAIM_ERROR);
			g_free(dialog_msg);
		} break;

		case 0x0008: { /* Someone has granted you authorization */
			char *dialog_msg;
			dialog_msg = g_strdup_printf(_("The user %lu has granted your request to add them to your contact list."), msg->sender);
			do_error_dialog(_("ICQ Authorization Granted"), dialog_msg, GAIM_INFO);
			g_free(dialog_msg);
		} break;

		case 0x0012: {
			/* Ack for authorizing/denying someone.  Or possibly an ack for sending any system notice */
		} break;

		default: {
			debug_printf("unknown offline message type 0x%04x\n", msg->type);
		}
	}

	return 1;
}

static int gaim_offlinemsgdone(aim_session_t *sess, aim_frame_t *fr, ...)
{
	aim_icq_ackofflinemsgs(sess);
	return 1;
}

static int gaim_simpleinfo(aim_session_t *sess, aim_frame_t *fr, ...)
{
	struct gaim_connection *gc = sess->aux_data;
	struct buddy *budlight;
	va_list ap;
	struct aim_icq_simpleinfo *info;
	char buf[16 * 1024];
	char who[16];

	va_start(ap, fr);
	info = va_arg(ap, struct aim_icq_simpleinfo *);
	va_end(ap);

	g_snprintf(who, sizeof who, "%lu", info->uin);
	g_snprintf(buf, sizeof buf,
		   "<B>UIN:</B> %lu<BR>"
		   "<B>Nick:</B> %s<BR>"
		   "<B>Name:</B> %s %s<BR>"
		   "<B>Email:</B> %s<BR>\n",
		   info->uin,
		   info->nick,
		   info->first, info->last,
		   info->email);

	/* If the contact is away, then we also want to get their status message
	 * and show it in the same window as info.  g_show_info_text gets the status 
	 * message if the third arg is 0 (this seems really gross to me).  The 
	 * parse-icq-status-message function knows if it is putting it's message in 
	 * an info window because the name will _not_ be in od->evilhack.  For getting 
	 * only the away message the contact's UIN is put in od->evilhack. */
	if ((budlight = find_buddy(gc, who))) {
		if ((budlight->uc >> 7) & (AIM_ICQ_STATE_AWAY || AIM_ICQ_STATE_DND || AIM_ICQ_STATE_OUT || AIM_ICQ_STATE_BUSY || AIM_ICQ_STATE_CHAT)) {
			if (budlight->caps & AIM_CAPS_ICQSERVERRELAY)
				g_show_info_text(gc, who, 0, buf, NULL);
			else {
				char *state_msg = gaim_icq_status((budlight->uc & 0xff80) >> 7);
				g_show_info_text(gc, who, 2, buf, "<B>Status:</B> ", state_msg, "<BR>\n<HR><BR><I>Remote client does not support sending status messages.</I><BR>\n", NULL);
				free(state_msg);
			}
		} else {
			char *state_msg = gaim_icq_status((budlight->uc & 0xff80) >> 7);
			g_show_info_text(gc, who, 2, buf, "<B>Status:</B> ", state_msg, NULL);
			free(state_msg);
		}
	} else
		g_show_info_text(gc, who, 2, buf, NULL);

	return 1;
}

static int gaim_popup(aim_session_t *sess, aim_frame_t *fr, ...)
{
	char *msg, *url;
	fu16_t wid, hei, delay;
	va_list ap;

	va_start(ap, fr);
	msg = va_arg(ap, char *);
	url = va_arg(ap, char *);
	wid = (fu16_t)va_arg(ap, int);
	hei = (fu16_t)va_arg(ap, int);
	delay = (fu16_t)va_arg(ap, int);
	va_end(ap);

	serv_got_popup(msg, url, wid, hei);

	return 1;
}

static int gaim_parse_searchreply(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	char *address, *SNs;
	int i, num;
	char *buf;
	int at = 0, len;

	va_start(ap, fr);
	address = va_arg(ap, char *);
	num = va_arg(ap, int);
	SNs = va_arg(ap, char *);
	va_end(ap);

	len = num * (MAXSNLEN + 1) + 1024;
	buf = g_malloc(len);
	at += g_snprintf(buf + at, len - at, "<B>%s has the following screen names:</B><BR>", address);
	for (i = 0; i < num; i++)
		at += g_snprintf(buf + at, len - at, "%s<BR>", &SNs[i * (MAXSNLEN + 1)]);
	g_show_info_text(NULL, NULL, 2, buf, NULL);
	g_free(buf);

	return 1;
}

static int gaim_parse_searcherror(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	char *address;
	char buf[BUF_LONG];

	va_start(ap, fr);
	address = va_arg(ap, char *);
	va_end(ap);

	g_snprintf(buf, sizeof(buf), "No results found for email address %s", address);
	do_error_dialog(buf, NULL, GAIM_ERROR);

	return 1;
}

static int gaim_account_confirm(aim_session_t *sess, aim_frame_t *fr, ...) {
	fu16_t status;
	va_list ap;
	char msg[256];
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	status = (fu16_t)va_arg(ap, unsigned int); /* status code of confirmation request */
	va_end(ap);

	debug_printf("account confirmation returned status 0x%04x (%s)\n", status,
			status ? "email sent" : "unknown");
	if (status) {
		g_snprintf(msg, sizeof(msg), "You should receive an email asking to confirm %s.",
				gc->username);
		do_error_dialog("Account confirmation requested.", msg, GAIM_INFO);
	}

	return 1;
}

static int gaim_info_change(aim_session_t *sess, aim_frame_t *fr, ...) {
	int change, str;
	fu16_t perms, type, length;
	char *val;
	va_list ap;
	char buf[BUF_LONG];
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	change = va_arg(ap, int);
	perms = (fu16_t)va_arg(ap, unsigned int);
	type = (fu16_t)va_arg(ap, unsigned int);
	length = (fu16_t)va_arg(ap, unsigned int);
	val = va_arg(ap, char *);
	str = va_arg(ap, int);
	va_end(ap);

	debug_printf("info%s: perms = %d, type = %x, length = %d, val = %s\n",
			change ? " change" : "", perms, type, length, str ? val : "(not string)");

	/* XXX Do something for other types too. */
	if ((type == 0x0011) && str && length) {
		g_snprintf(buf, sizeof(buf), "The email address for %s is %s", gc->username, val);
		do_error_dialog(buf, NULL, GAIM_INFO);
	}

	return 1;
}

static void oscar_keepalive(struct gaim_connection *gc) {
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;
	aim_flap_nop(odata->sess, odata->conn);
}

static char *oscar_name() {
	return "AIM / ICQ";
}

static int oscar_send_typing(struct gaim_connection *gc, char *name, int typing) {
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;
	struct direct_im *dim = find_direct_im(odata, name);
	if (!dim)
		return 0;
	aim_send_typing(odata->sess, dim->conn, typing);
	return 0;
}
static void oscar_ask_direct_im(struct gaim_connection *gc, char *name);

static int oscar_send_im(struct gaim_connection *gc, char *name, char *message, int len, int imflags) {
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;
	struct direct_im *dim = find_direct_im(odata, name);
	int ret = 0;

	if (dim) {
		if (dim->connected) {  /* If we're not connected yet, send through server */
			ret =  aim_send_im_direct(odata->sess, dim->conn, message, len == -1 ? strlen(message) : len);
			if (ret == 0)
				return 1;
			else return ret;
		}
		debug_printf("Direct IM pending, but not connected; sending through server\n");
	} else if (len != -1) {
		/* Trying to send an IM image outside of a direct connection. */
		oscar_ask_direct_im(gc, name);
		return -ENOTCONN;
	}
	if (imflags & IM_FLAG_AWAY) {
		ret = aim_send_im(odata->sess, name, AIM_IMFLAGS_AWAY, message);
	} else {
		struct aim_sendimext_args args;
		GSList *h = odata->hasicons;
		struct icon_req *ir = NULL;
		char *who = normalize(name);
		struct stat st;
		
		args.flags = AIM_IMFLAGS_ACK | AIM_IMFLAGS_CUSTOMFEATURES;
		if (odata->icq)
			args.flags |= AIM_IMFLAGS_OFFLINE;
		
		args.features = gaim_features;
		args.featureslen = sizeof(gaim_features);
		
		while (h) {
			ir = h->data;
			if (ir->request && !strcmp(who, ir->user))
				break;
			h = h->next;
			}
		if (h) {
			ir->request = FALSE;
			args.flags |= AIM_IMFLAGS_BUDDYREQ;
			debug_printf("sending buddy icon request with message\n");
		}
		
		if (gc->user->iconfile[0] && !stat(gc->user->iconfile, &st)) {
			FILE *file = fopen(gc->user->iconfile, "r");
			if (file) {
				char *buf = g_malloc(st.st_size);
				fread(buf, 1, st.st_size, file);
				
				args.iconlen   = st.st_size;
				args.iconsum   = aim_iconsum(buf, st.st_size);
				args.iconstamp = st.st_mtime;

				args.flags |= AIM_IMFLAGS_HASICON;
				debug_printf("Claiming to have an icon.\n");

				fclose(file);
				g_free(buf);
			}
		}
		
		args.destsn = name;
		args.msg    = message;
		args.msglen = strlen(message);
		
		ret = aim_send_im_ext(odata->sess, &args);
		
	}
	if (ret >= 0)
		return 1;
	return ret;
}

static void oscar_get_info(struct gaim_connection *g, char *name) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (odata->icq)
		aim_icq_getsimpleinfo(odata->sess, name);
	else
		/* people want the away message on the top, so we get the away message
		 * first and then get the regular info, since it's too difficult to
		 * insert in the middle. i hate people. */
		aim_getinfo(odata->sess, odata->conn, name, AIM_GETINFO_AWAYMESSAGE);
}

static void oscar_get_away(struct gaim_connection *g, char *who) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (odata->icq) {
		struct buddy *budlight = find_buddy(g, who);
		if (budlight)
			if ((budlight->uc & 0xff80) >> 7)
				if (budlight->caps & AIM_CAPS_ICQSERVERRELAY)
					aim_send_im_ch2_geticqmessage(odata->sess, who, (budlight->uc & 0xff80) >> 7);
				else
					debug_printf("Error: Remote client does not support retrieval of status messages.\n");
			else
				debug_printf("Error: The user %s has no status message, therefore not requesting.\n", who);
		else
			debug_printf("Error: Could not find %s in local contact list, therefore unable to request status message.\n", who);
	} else
		aim_getinfo(odata->sess, odata->conn, who, AIM_GETINFO_GENERALINFO);
}

static void oscar_get_caps(struct gaim_connection *g, char *name) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	aim_getinfo(odata->sess, odata->conn, name, AIM_GETINFO_CAPABILITIES);
}

static void oscar_set_dir(struct gaim_connection *g, char *first, char *middle, char *last,
			  char *maiden, char *city, char *state, char *country, int web) {
	/* FIXME : some of these things are wrong, but i'm lazy */
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	aim_setdirectoryinfo(odata->sess, odata->conn, first, middle, last,
				maiden, NULL, NULL, city, state, NULL, 0, web);
}


static void oscar_set_idle(struct gaim_connection *g, int time) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	aim_bos_setidle(odata->sess, odata->conn, time);
}

static void oscar_set_info(struct gaim_connection *g, char *info) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	gchar *inforeal;

	if (odata->rights.maxsiglen == 0)
		do_error_dialog(_("Unable to set AIM profile."), 
				_("You have probably requested to set your profile before the login procedure completed.  "
				  "Your profile remains unset; try setting it again when you are fully connected."), GAIM_ERROR);

	if (strlen(info) > odata->rights.maxsiglen) {
		gchar *errstr;

		errstr = g_strdup_printf(_("The maximum profile length of %d bytes has been exceeded.  "
					   "Gaim has truncated and set it."), odata->rights.maxsiglen);
		do_error_dialog("Profile too long.", errstr, GAIM_WARNING);

		g_free(errstr);
	}

	inforeal = g_strndup(info, odata->rights.maxsiglen);

	if (odata->icq)
		aim_bos_setprofile(odata->sess, odata->conn, NULL, NULL, caps_icq);
	else
		aim_bos_setprofile(odata->sess, odata->conn, inforeal, NULL, caps_aim);

	g_free(inforeal);

	return;
}

static void oscar_set_away_aim(struct gaim_connection *gc, struct oscar_data *od, const char *message)
{

	if (od->rights.maxawaymsglen == 0)
		do_error_dialog(_("Unable to set AIM away message."), 
				_("You have probably requested to set your away message before the login procedure completed.  "
				  "You remain in a \"present\" state; try setting it again when you are fully connected."), GAIM_ERROR);
	
	if (gc->away)
		g_free(gc->away);
	gc->away = NULL;

	if (!message) {
		aim_bos_setprofile(od->sess, od->conn, NULL, "", caps_aim);
		return;
	}

	if (strlen(message) > od->rights.maxawaymsglen) {
		gchar *errstr;

		errstr = g_strdup_printf(_("The away message length of %d bytes has been exceeded.  "
					   "Gaim has truncated it and set you away."), od->rights.maxawaymsglen);
		do_error_dialog("Away message too long.", errstr, GAIM_WARNING);
		g_free(errstr);
	}

	gc->away = g_strndup(message, od->rights.maxawaymsglen);
	aim_bos_setprofile(od->sess, od->conn, NULL, gc->away, caps_aim);

	return;
}

static void oscar_set_away_icq(struct gaim_connection *gc, struct oscar_data *od, const char *state, const char *message)
{

	if (gc->away)
		gc->away = NULL;

	if (!strcmp(state, "Online"))
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_NORMAL);
	else if (!strcmp(state, "Away")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_AWAY);
		gc->away = "";
	} else if (!strcmp(state, "Do Not Disturb")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_AWAY | AIM_ICQ_STATE_DND | AIM_ICQ_STATE_BUSY);
		gc->away = "";
	} else if (!strcmp(state, "Not Available")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_OUT | AIM_ICQ_STATE_AWAY);
		gc->away = "";
	} else if (!strcmp(state, "Occupied")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_AWAY | AIM_ICQ_STATE_BUSY);
		gc->away = "";
	} else if (!strcmp(state, "Free For Chat")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_CHAT);
		gc->away = "";
	} else if (!strcmp(state, "Invisible")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_INVISIBLE);
		gc->away = "";
	} else if (!strcmp(state, GAIM_AWAY_CUSTOM)) {
	 	if (message) {
			aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_OUT | AIM_ICQ_STATE_AWAY);
			gc->away = "";
		} else {
			aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_NORMAL);
		}
	}

	return;
}

static void oscar_set_away(struct gaim_connection *gc, char *state, char *message)
{
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;

	if (od->icq)
		oscar_set_away_icq(gc, od, state, message);
	else
		oscar_set_away_aim(gc, od, message);

	return;
}

static void oscar_warn(struct gaim_connection *g, char *name, int anon) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	aim_send_warning(odata->sess, odata->conn, name, anon ? AIM_WARN_ANON : 0);
}

static void oscar_dir_search(struct gaim_connection *g, char *first, char *middle, char *last,
			     char *maiden, char *city, char *state, char *country, char *email) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (strlen(email))
		aim_usersearch_address(odata->sess, odata->conn, email);
}

static void oscar_add_buddy(struct gaim_connection *g, char *name) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (odata->icq) {
		aim_add_buddy(odata->sess, odata->conn, name);
	} else {
		if ((odata->sess->ssi.received_data) && !(aim_ssi_itemlist_finditem(odata->sess->ssi.items, NULL, name, 0x0000))) {
			debug_printf("ssi: adding buddy %s to group %s\n", name, find_group_by_buddy(g, name)->name);
			aim_ssi_addbuddies(odata->sess, odata->conn, find_group_by_buddy(g, name)->name, &name, 1);
		}
	}
}

static void oscar_add_buddies(struct gaim_connection *g, GList *buddies) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (odata->icq) {
		char buf[MSG_LEN];
		int n=0;
		while (buddies) {
			if (n > MSG_LEN - 18) {
				aim_bos_setbuddylist(odata->sess, odata->conn, buf);
				n = 0;
			}
			n += g_snprintf(buf + n, sizeof(buf) - n, "%s&", (char *)buddies->data);
			buddies = buddies->next;
		}
		aim_bos_setbuddylist(odata->sess, odata->conn, buf);
	} else {
		if (odata->sess->ssi.received_data) {
			int tmp;
			GSList *curgrp, *curbud;
			for (curgrp=g->groups; curgrp; curgrp=g_slist_next(curgrp)) {
				tmp = 0;
				for (curbud=((struct group*)curgrp->data)->members; curbud; curbud=curbud->next)
					if (!aim_ssi_itemlist_finditem(odata->sess->ssi.items, NULL, ((struct buddy*)curbud->data)->name, 0x0000))
						tmp++;
				if (tmp) {
					char **sns = (char **)malloc(tmp*sizeof(char*));
					tmp = 0;
					for (curbud=((struct group*)curgrp->data)->members; curbud; curbud=curbud->next)
						if (!aim_ssi_itemlist_finditem(odata->sess->ssi.items, NULL, ((struct buddy*)curbud->data)->name, 0x0000)) {
							debug_printf("ssi: adding buddy %s to group %s\n", ((struct buddy*)curbud->data)->name, ((struct group*)curgrp->data)->name);
							sns[tmp] = (char *)((struct buddy*)curbud->data)->name;
							tmp++;
						}
					aim_ssi_addbuddies(odata->sess, odata->conn, ((struct group*)curgrp->data)->name, sns, tmp);
					free(sns);
				}
			}
		}
	}
}

static void oscar_move_buddy(struct gaim_connection *g, char *name, char *old_group, char *new_group) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (!odata->icq)
		if (odata->sess->ssi.received_data) {
			aim_ssi_movebuddy(odata->sess, odata->conn, old_group, new_group, name);
			debug_printf("ssi: moved buddy %s from group %s to group %s\n", name, old_group, new_group);
		}
}

static void oscar_remove_buddy(struct gaim_connection *g, char *name, char *group) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (odata->icq) {
		aim_remove_buddy(odata->sess, odata->conn, name);
	} else {
		if (odata->sess->ssi.received_data) {
			struct aim_ssi_item *ssigroup;
			while (aim_ssi_itemlist_finditem(odata->sess->ssi.items, NULL, name, 0x0000) && (ssigroup = aim_ssi_itemlist_findparent(odata->sess->ssi.items, name)) && !aim_ssi_delbuddies(odata->sess, odata->conn, ssigroup->name, &name, 1))
				debug_printf("ssi: deleted buddy %s from group %s\n", name, group);
		}
	}
}

static void oscar_remove_buddies(struct gaim_connection *g, GList *buddies, char *group) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (odata->icq) {
		GList *cur;
		for (cur=buddies; cur; cur=cur->next)
			aim_remove_buddy(odata->sess, odata->conn, cur->data);
	} else {
		if (odata->sess->ssi.received_data) {
			GList *cur;
			int tmp = 0;
			for (cur=buddies; cur; cur=cur->next)
				if (aim_ssi_itemlist_finditem(odata->sess->ssi.items, NULL, cur->data, 0x0000))
					tmp++;
			if (tmp) {
				char **sns;
				sns = (char **)malloc(tmp*sizeof(char*));
				tmp = 0;
				for (cur=buddies; cur; cur=cur->next)
					if (aim_ssi_itemlist_finditem(odata->sess->ssi.items, NULL, cur->data, 0x0000)) {
						debug_printf("ssi: deleting buddy %s from group %s\n", cur->data, group);
						sns[tmp] = cur->data;
						tmp++;
					}
				aim_ssi_delbuddies(odata->sess, odata->conn, group, sns, tmp);
				free(sns);
			}
		}
	}
}

static void oscar_rename_group(struct gaim_connection *g, char *old_group, char *new_group, GList *members) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (!odata->icq)
		if (odata->sess->ssi.received_data) {
			if (aim_ssi_itemlist_finditem(odata->sess->ssi.items, NULL, new_group, 0x0001)) {
				oscar_remove_buddies(g, members, old_group);
				oscar_add_buddies(g, members);
				debug_printf("ssi: moved all buddies from group %s to %s\n", old_group, new_group);
			} else {
				aim_ssi_rename_group(odata->sess, odata->conn, old_group, new_group);
				debug_printf("ssi: renamed group %s to %s\n", old_group, new_group);
			}
		}
}

static int gaim_ssi_parserights(aim_session_t *sess, aim_frame_t *fr, ...) {
/*	XXX - Fix parsing of the ssi rights packet and pass us the data
	fu16_t maxbuddies, maxgroups, maxpermits, maxdenies;
	va_list ap;

	va_start(ap, fr);
	maxbuddies = (fu16_t)va_arg(ap, unsigned int);
	maxgroupss = (fu16_t)va_arg(ap, unsigned int);
	maxpermits = (fu16_t)va_arg(ap, unsigned int);
	maxdenies = (fu16_t)va_arg(ap, unsigned int);
	va_end(ap);

	debug_printf("ssi rights: Max buddies = %d / Max groups = %d / Max permits = %d / Max denies = %d\n", maxbuddies, maxgroups, maxpermits, maxdenies);
*/

	return 1;
}

static int gaim_ssi_parselist(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;
	struct aim_ssi_item *curitem;
	int tmp;
	char **sns;

	debug_printf("ssi: syncing local list and server list\n");

	if (odata->icq)
		return 1;

	/* Activate SSI */
	debug_printf("ssi: activating server-stored buddy list\n");
	aim_ssi_enable(sess, fr->conn);

	/* Clean the buddy list */
	aim_ssi_cleanlist(sess, fr->conn);

	/* Add from server list to local list */
	tmp = 0;
	for (curitem=sess->ssi.items; curitem; curitem=curitem->next) {
		switch (curitem->type) {
			case 0x0000: /* Buddy */
				if ((curitem->name) && (!find_buddy(gc, curitem->name))) {
					struct aim_ssi_item *curgroup = sess->ssi.items;
					while (curgroup) {
						if ((curgroup->type == 0x0001) && (curgroup->gid == curitem->gid) && (curgroup->name)) {
							debug_printf("ssi: adding buddy %s to group %s to local list\n", curitem->name, curgroup->name);
							add_buddy(gc, curgroup->name, curitem->name, 0);
							tmp++;
						}
						curgroup = curgroup->next;
					}
				}
				break;

			case 0x0002: /* Permit buddy */
				if (curitem->name) {
					GSList *list;
					for (list=gc->permit; (list && aim_sncmp(curitem->name, list->data)); list=list->next);
					if (!list) {
						char *name;
						debug_printf("ssi: adding permit buddy %s to local list\n", curitem->name);
						name = g_strdup(normalize(curitem->name));
						gc->permit = g_slist_append(gc->permit, name);
						build_allow_list();
						tmp++;
					}
				}
				break;

			case 0x0003: /* Deny buddy */
				if (curitem->name) {
					GSList *list;
					for (list=gc->deny; (list && aim_sncmp(curitem->name, list->data)); list=list->next);
					if (!list) {
						char *name;
						debug_printf("ssi: adding deny buddy %s to local list\n", curitem->name);
						name = g_strdup(normalize(curitem->name));
						gc->deny = g_slist_append(gc->deny, name);
						build_block_list();
						tmp++;
					}
				}
				break;

			case 0x0004: /* Permit/deny setting */
				if (curitem->data) {
					fu8_t permdeny;
					if ((permdeny = aim_ssi_getpermdeny(sess->ssi.items)) && (permdeny != gc->permdeny)) {
						debug_printf("ssi: changing permdeny from %d to %d\n", gc->permdeny, permdeny);
						gc->permdeny = permdeny;
						tmp++;
					}
				}
				break;

			case 0x0005: /* Presence setting */
				/* We don't want to change Gaim's setting because it applies to all accounts */
				break;
		} /* End of switch on curitem->type */
	} /* End of for loop */
	if (tmp)
		do_export(gc);

	/* Add from local list to server list */
	if (gc) {
		GSList *cur;

		/* Buddies */
		cur = gc->groups;
		while (cur) {
			GSList *curbud;
			tmp = 0;
			for (curbud=((struct group*)cur->data)->members; curbud; curbud=curbud->next)
				if (!aim_ssi_itemlist_finditem(sess->ssi.items, NULL, ((struct buddy*)curbud->data)->name, 0x0000))
					tmp++;
			if (tmp) {
				sns = (char **)malloc(tmp*sizeof(char*));
				tmp = 0;
				for (curbud=((struct group*)cur->data)->members; curbud; curbud=curbud->next)
					if (!aim_ssi_itemlist_finditem(sess->ssi.items, NULL, ((struct buddy*)curbud->data)->name, 0x0000)) {
						debug_printf("ssi: adding buddy %s from local list to server list\n", ((struct buddy*)curbud->data)->name);
						sns[tmp] = ((char *)((struct buddy*)curbud->data)->name);
						tmp++;
					}
				aim_ssi_addbuddies(sess, fr->conn, ((struct group*)cur->data)->name, sns, tmp);
				free(sns);
			}
			cur = g_slist_next(cur);
		}

		/* Permit list */
		if (gc->permit) {
			tmp = 0;
			for (cur=gc->permit; cur; cur=cur->next)
				if (!aim_ssi_itemlist_finditem(sess->ssi.items, NULL, cur->data, 0x0002))
					tmp++;
			if (tmp) {
				sns = (char **)malloc(tmp*sizeof(char*));
				tmp = 0;
				for (cur=gc->permit; cur; cur=cur->next)
					if (!aim_ssi_itemlist_finditem(sess->ssi.items, NULL, cur->data, 0x0002)) {
						debug_printf("ssi: adding permit %s from local list to server list\n", cur->data);
						sns[tmp] = cur->data;
						tmp++;
					}
				aim_ssi_addpord(sess, fr->conn, sns, tmp, AIM_SSI_TYPE_PERMIT);
				free(sns);
			}
		}

		/* Deny list */
		if (gc->deny) {
			tmp = 0;
			for (cur=gc->deny; cur; cur=cur->next)
				if (!aim_ssi_itemlist_finditem(sess->ssi.items, NULL, cur->data, 0x0003))
					tmp++;
			if (tmp) {
				sns = (char **)malloc(tmp*sizeof(char*));
				tmp = 0;
				for (cur=gc->deny; cur; cur=cur->next)
					if (!aim_ssi_itemlist_finditem(sess->ssi.items, NULL, cur->data, 0x0003)) {
						debug_printf("ssi: adding deny %s from local list to server list\n", cur->data);
						sns[tmp] = cur->data;
						tmp++;
					}
				aim_ssi_addpord(sess, fr->conn, sns, tmp, AIM_SSI_TYPE_DENY);
				free(sns);
			}
		}

		/* Presence settings (idle time visibility) */
		if ((tmp = aim_ssi_getpresence(sess->ssi.items)) != 0xFFFFFFFF)
			if (report_idle && !(tmp & 0x400))
				aim_ssi_setpresence(sess, fr->conn, tmp | 0x400);

		/* Check for maximum number of buddies */
		for (cur=gc->groups, tmp=0; cur; cur=g_slist_next(cur)) {
			struct group* gr = (struct group*)cur->data;
			tmp = tmp + g_slist_length(gr->members);
		}
		if (tmp > odata->rights.maxbuddies) {
			char *dialog_msg = g_strdup_printf(_("The maximum number of buddies allowed in your buddy list is %d, and you have %d."
							     "  Until you are below the limit, some buddies will not show up as online."), 
							   odata->rights.maxbuddies, tmp);
			do_error_dialog("Maximum buddy list length exceeded.", dialog_msg, GAIM_WARNING);
			g_free(dialog_msg);
		}
		
	} /* end if (gc) */

	return 1;
}

static GList *oscar_chat_info(struct gaim_connection *gc) {
	GList *m = NULL;
	struct proto_chat_entry *pce;

	pce = g_new0(struct proto_chat_entry, 1);
	pce->label = _("Join what group:");
	m = g_list_append(m, pce);

	pce = g_new0(struct proto_chat_entry, 1);
	pce->label = _("Exchange:");
	pce->is_int = TRUE;
	pce->min = 4;
	pce->max = 20;
	m = g_list_append(m, pce);

	return m;
}

static void oscar_join_chat(struct gaim_connection *g, GList *data) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	aim_conn_t *cur;
	char *name;
	int *exchange;

	if (!data || !data->next)
		return;

	name = data->data;
	exchange = data->next->data;

	debug_printf("Attempting to join chat room %s.\n", name);
	if ((cur = aim_getconn_type(odata->sess, AIM_CONN_TYPE_CHATNAV))) {
		debug_printf("chatnav exists, creating room\n");
		aim_chatnav_createroom(odata->sess, cur, name, *exchange);
	} else {
		/* this gets tricky */
		struct create_room *cr = g_new0(struct create_room, 1);
		debug_printf("chatnav does not exist, opening chatnav\n");
		cr->exchange = *exchange;
		cr->name = g_strdup(name);
		odata->create_rooms = g_slist_append(odata->create_rooms, cr);
		aim_reqservice(odata->sess, odata->conn, AIM_CONN_TYPE_CHATNAV);
	}
}

static void oscar_chat_invite(struct gaim_connection *g, int id, char *message, char *name) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	struct chat_connection *ccon = find_oscar_chat(g, id);
	
	if (!ccon)
		return;
	
	aim_chat_invite(odata->sess, odata->conn, name, message ? message : "",
			ccon->exchange, ccon->name, 0x0);
}

static void oscar_chat_leave(struct gaim_connection *g, int id) {
	struct oscar_data *odata = g ? (struct oscar_data *)g->proto_data : NULL;
	GSList *bcs = g->buddy_chats;
	struct conversation *b = NULL;
	struct chat_connection *c = NULL;
	int count = 0;

	while (bcs) {
		count++;
		b = (struct conversation *)bcs->data;
		if (id == b->id)
			break;
		bcs = bcs->next;
		b = NULL;
	}

	if (!b)
		return;

	debug_printf("Attempting to leave room %s (currently in %d rooms)\n", b->name, count);
	
	c = find_oscar_chat(g, b->id);
	if (c != NULL) {
		if (odata)
			odata->oscar_chats = g_slist_remove(odata->oscar_chats, c);
		if (c->inpa > 0)
			gaim_input_remove(c->inpa);
		if (g && odata->sess)
			aim_conn_kill(odata->sess, &c->conn);
		g_free(c->name);
		g_free(c->show);
		g_free(c);
	}
	/* we do this because with Oscar it doesn't tell us we left */
	serv_got_chat_left(g, b->id);
}

static int oscar_chat_send(struct gaim_connection *g, int id, char *message) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	GSList *bcs = g->buddy_chats;
	struct conversation *b = NULL;
	struct chat_connection *c = NULL;
	char *buf, *buf2;
	int i, j;

	while (bcs) {
		b = (struct conversation *)bcs->data;
		if (id == b->id)
			break;
		bcs = bcs->next;
		b = NULL;
	}
	if (!b)
		return -EINVAL;

	bcs = odata->oscar_chats;
	while (bcs) {
		c = (struct chat_connection *)bcs->data;
		if (b == c->cnv)
			break;
		bcs = bcs->next;
		c = NULL;
	}
	if (!c)
		return -EINVAL;

	buf = g_malloc(strlen(message) * 4 + 1);
	for (i = 0, j = 0; i < strlen(message); i++) {
		if (message[i] == '\n') {
			buf[j++] = '<';
			buf[j++] = 'B';
			buf[j++] = 'R';
			buf[j++] = '>';
		} else {
			buf[j++] = message[i];
		}
	}
	buf[j] = '\0';

	if (strlen(buf) > c->maxlen)
		return -E2BIG;

	buf2 = strip_html(buf);
	if (strlen(buf2) > c->maxvis) {
		g_free(buf2);
		return -E2BIG;
	}
	g_free(buf2);

	aim_chat_send_im(odata->sess, c->conn, 0, buf, strlen(buf));
	g_free(buf);
	return 0;
}

static char **oscar_list_icon(int uc) {
	if (uc == 0)
		return (char **)icon_online_xpm;
	if (uc & 0xff80) {
		uc >>= 7;
		if (uc & AIM_ICQ_STATE_INVISIBLE)
			return icon_offline_xpm;
		if (uc & AIM_ICQ_STATE_CHAT)
			return icon_ffc_xpm;
		if (uc & AIM_ICQ_STATE_DND)
		 	return icon_dnd_xpm;
		if (uc & AIM_ICQ_STATE_OUT)
			return icon_na_xpm;
		if (uc & AIM_ICQ_STATE_BUSY)
		 	return icon_occ_xpm;
		if (uc & AIM_ICQ_STATE_AWAY)
			return icon_away_xpm;
		return icon_online_xpm;
	}
	if (uc & UC_UNAVAILABLE)
		return (char **)away_icon_xpm;
	if (uc & UC_WIRELESS)
		return (char **)wireless_icon_xpm;
	if (uc & UC_AB)
		return (char **)ab_xpm;
	if (uc & UC_AOL)
		return (char **)aol_icon_xpm;
	if (uc & UC_ADMIN)
		return (char **)admin_icon_xpm;
	if (uc & UC_UNCONFIRMED)
		return (char **)dt_icon_xpm;
	if (uc & UC_NORMAL)
		return (char **)free_icon_xpm;
	return NULL;
}

static int gaim_directim_initiate(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	aim_conn_t *newconn, *listenerconn;
	struct conversation *cnv;
	struct direct_im *dim;
	char buf[256];
	char *sn;

	va_start(ap, fr);
	newconn = va_arg(ap, aim_conn_t *);
	listenerconn = va_arg(ap, aim_conn_t *);
	va_end(ap);

	aim_conn_close(listenerconn);
	aim_conn_kill(sess, &listenerconn);

	sn = g_strdup(aim_directim_getsn(newconn));

	debug_printf("DirectIM: initiate success to %s\n", sn);
	dim = find_direct_im(od, sn);

	if (!(cnv = find_conversation(sn)))
		cnv = new_conversation(sn);
	gaim_input_remove(dim->watcher);
	dim->conn = newconn;
	dim->watcher = gaim_input_add(dim->conn->fd, GAIM_INPUT_READ,
					oscar_callback, dim->conn);
	dim->connected = TRUE;
	g_snprintf(buf, sizeof buf, _("Direct IM with %s established"), sn);
	g_free(sn);
	write_to_conv(cnv, buf, WFLAG_SYSTEM, NULL, time(NULL), -1);

	aim_conn_addhandler(sess, newconn, AIM_CB_FAM_OFT, AIM_CB_OFT_DIRECTIMINCOMING,
				gaim_directim_incoming, 0);
	aim_conn_addhandler(sess, newconn, AIM_CB_FAM_OFT, AIM_CB_OFT_DIRECTIMTYPING,
				gaim_directim_typing, 0);
	aim_conn_addhandler(sess, newconn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_IMAGETRANSFER,
			    gaim_update_ui, 0);
	return 1;
}

static int gaim_update_ui(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	char *sn;
	double percent;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	struct conversation *c;
	struct direct_im *dim;

	va_start(ap, fr);
	sn = va_arg(ap, char *);
	percent = va_arg(ap, double);
	va_end(ap);
	
	if (!(dim = find_direct_im(od, sn)))
		return 1;
	if (dim->watcher) {
		gaim_input_remove(dim->watcher);   /* Otherwise, the callback will callback */
		dim->watcher = 0;
	}
	while (gtk_events_pending())
		gtk_main_iteration();
	
	if ((c = find_conversation(sn)))
		update_progress(c, percent);
	dim->watcher = gaim_input_add(dim->conn->fd, GAIM_INPUT_READ,
				      oscar_callback, dim->conn);

	return 1;
}

static int gaim_directim_incoming(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	char *msg, *sn;
	int len;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	sn = va_arg(ap, char *);
	msg = va_arg(ap, char *);
	len = va_arg(ap, int);
	va_end(ap);

	debug_printf("Got DirectIM message from %s\n", sn);

	serv_got_im(gc, sn, msg, 0, time(NULL), len);

	return 1;
}

static int gaim_directim_typing(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	char *sn;
	int typing;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	sn = va_arg(ap, char *);
	typing = va_arg(ap, int);
	va_end(ap);

	if (typing) {
		/* I had to leave this. It's just too funny. It reminds me of my sister. */
		debug_printf("ohmigod! %s has started typing (DirectIM). He's going to send you a message! *squeal*\n", sn);
		serv_got_typing(gc,sn,0);
	} else
		serv_got_typing_stopped(gc,sn);
	return 1;
}

struct ask_do_dir_im {
	char *who;
	struct gaim_connection *gc;
};

static void oscar_cancel_direct_im(gpointer obj, struct ask_do_dir_im *data) {
	g_free(data);
}

static void oscar_direct_im(gpointer obj, struct ask_do_dir_im *data) {
	struct gaim_connection *gc = data->gc;
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	struct direct_im *dim;

	dim = find_direct_im(od, data->who);
	if (dim) {
		if (!(dim->connected)) {  /* We'll free the old, unconnected dim, and start over */
			od->direct_ims = g_slist_remove(od->direct_ims, dim);
			gaim_input_remove(dim->watcher);
			g_free(dim);
			debug_printf("Gave up on old direct IM, trying again\n");
		} else {
			do_error_dialog("DirectIM already open.", NULL, GAIM_ERROR);
			return;
		}
	}
	dim = g_new0(struct direct_im, 1);
	dim->gc = gc;
	g_snprintf(dim->name, sizeof dim->name, "%s", data->who);

	dim->conn = aim_directim_initiate(od->sess, data->who);
	if (dim->conn != NULL) {
		od->direct_ims = g_slist_append(od->direct_ims, dim);
		dim->watcher = gaim_input_add(dim->conn->fd, GAIM_INPUT_READ,
						oscar_callback, dim->conn);
		aim_conn_addhandler(od->sess, dim->conn, AIM_CB_FAM_OFT, AIM_CB_OFT_DIRECTIMINITIATE,
					gaim_directim_initiate, 0);
	} else {
		do_error_dialog(_("Unable to open Direct IM"), NULL, GAIM_ERROR);
		g_free(dim);
	}
}

static void oscar_ask_direct_im(struct gaim_connection *gc, gchar *who) {
	char buf[BUF_LONG];
	struct ask_do_dir_im *data = g_new0(struct ask_do_dir_im, 1);
	data->who = who;
	data->gc = gc;
	g_snprintf(buf, sizeof(buf),  _("You have selected to open a Direct IM connection with %s."
					" Doing this will let them see your IP address, and may be"
					" a security risk. Do you wish to continue?"), who);
	do_ask_dialog(buf, data, oscar_direct_im, oscar_cancel_direct_im);
}

static void oscar_get_away_msg(struct gaim_connection *gc, char *who) {
	struct oscar_data *od = gc->proto_data;
	od->evilhack = g_slist_append(od->evilhack, g_strdup(normalize(who)));
	if (od->icq) {
		struct buddy *budlight = find_buddy(gc, who);
		if (budlight)
			if ((budlight->uc >> 7) & (AIM_ICQ_STATE_AWAY || AIM_ICQ_STATE_DND || AIM_ICQ_STATE_OUT || AIM_ICQ_STATE_BUSY || AIM_ICQ_STATE_CHAT))
				if (budlight->caps & AIM_CAPS_ICQSERVERRELAY)
					aim_send_im_ch2_geticqmessage(od->sess, who, (budlight->uc & 0xff80) >> 7);
				else {
					char *state_msg = gaim_icq_status((budlight->uc & 0xff80) >> 7);
					char *dialog_msg = g_strdup_printf(_("<B>UIN:</B> %s<BR><B>Status:</B> %s<BR><HR><BR><I>Remote client does not support sending status messages.</I><BR>"), who, state_msg);
					g_show_info_text(gc, who, 2, dialog_msg, NULL);
					free(state_msg);
					free(dialog_msg);
				}
			else {
				char *state_msg = gaim_icq_status((budlight->uc & 0xff80) >> 7);
				char *dialog_msg = g_strdup_printf(_("<B>UIN:</B> %s<BR><B>Status:</B> %s<BR><HR><BR><I>User has no status message.</I><BR>"), who, state_msg);
				g_show_info_text(gc, who, 2, dialog_msg, NULL);
				free(state_msg);
				free(dialog_msg);
			}
		else
			do_error_dialog("Could not find contact in local list, therefore unable to request status message.\n", NULL, GAIM_ERROR);
	} else
		oscar_get_info(gc, who);
}

static GList *oscar_buddy_menu(struct gaim_connection *gc, char *who) {
	GList *m = NULL;
	struct proto_buddy_menu *pbm;
	char *n = g_strdup(normalize(gc->username));
	struct oscar_data *odata = gc->proto_data;

	pbm = g_new0(struct proto_buddy_menu, 1);
	pbm->label = _("Get Info");
	pbm->callback = oscar_get_info;
	pbm->gc = gc;
	m = g_list_append(m, pbm);

	if (odata->icq) {
		pbm = g_new0(struct proto_buddy_menu, 1);
		pbm->label = _("Get Status Msg");
		pbm->callback = oscar_get_away_msg;
		pbm->gc = gc;
		m = g_list_append(m, pbm);
	} else {
		pbm = g_new0(struct proto_buddy_menu, 1);
		pbm->label = _("Get Away Msg");
		pbm->callback = oscar_get_away_msg;
		pbm->gc = gc;
		m = g_list_append(m, pbm);

		if (strcmp(n, normalize(who))) {
			pbm = g_new0(struct proto_buddy_menu, 1);
			pbm->label = _("Direct IM");
			pbm->callback = oscar_ask_direct_im;
			pbm->gc = gc;
			m = g_list_append(m, pbm);
		}
	}

	pbm = g_new0(struct proto_buddy_menu, 1);
	pbm->label = _("Get Capabilities");
	pbm->callback = oscar_get_caps;
	pbm->gc = gc;
	m = g_list_append(m, pbm);

	g_free(n);

	return m;
}

static GList *oscar_edit_buddy_menu(struct gaim_connection *gc, char *who)
{
	GList *m = NULL;
	struct proto_buddy_menu *pbm;
	struct oscar_data *odata = gc->proto_data;

	if (odata->icq) {
		pbm = g_new0(struct proto_buddy_menu, 1);
		pbm->label = _("Get Info");
		pbm->callback = oscar_get_info;
		pbm->gc = gc;
		m = g_list_append(m, pbm);
	}

	return m;
}

static GList *oscar_user_opts()
{
	GList *m = NULL;
	struct proto_user_opt *puo;

	puo = g_new0(struct proto_user_opt, 1);
	puo->label = "Auth Host:";
	puo->def = "login.oscar.aol.com";
	puo->pos = USEROPT_AUTH;
	m = g_list_append(m, puo);

	puo = g_new0(struct proto_user_opt, 1);
	puo->label = "Auth Port:";
	puo->def = "5190";
	puo->pos = USEROPT_AUTHPORT;
	m = g_list_append(m, puo);

	return m;
}

static void oscar_set_permit_deny(struct gaim_connection *gc) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (od->icq) {
		GSList *list;
		char buf[MAXMSGLEN];
		int at;

		switch(gc->permdeny) {
		case 1:
			aim_bos_changevisibility(od->sess, od->conn, AIM_VISIBILITYCHANGE_DENYADD, gc->username);
			break;
		case 2:
			aim_bos_changevisibility(od->sess, od->conn, AIM_VISIBILITYCHANGE_PERMITADD, gc->username);
			break;
		case 3:
			list = gc->permit;
			at = 0;
			while (list) {
				at += g_snprintf(buf + at, sizeof(buf) - at, "%s&", (char *)list->data);
				list = list->next;
			}
			aim_bos_changevisibility(od->sess, od->conn, AIM_VISIBILITYCHANGE_PERMITADD, buf);
			break;
		case 4:
			list = gc->deny;
			at = 0;
			while (list) {
				at += g_snprintf(buf + at, sizeof(buf) - at, "%s&", (char *)list->data);
				list = list->next;
			}
			aim_bos_changevisibility(od->sess, od->conn, AIM_VISIBILITYCHANGE_DENYADD, buf);
			break;
			default:
			break;
		}
		signoff_blocked(gc);
	} else {
		if (od->sess->ssi.received_data)
			aim_ssi_setpermdeny(od->sess, od->conn, gc->permdeny, 0xffffffff);
	}
}

static void oscar_add_permit(struct gaim_connection *gc, char *who) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (od->icq) {
		if (gc->permdeny != 3) return;
		oscar_set_permit_deny(gc);
	} else {
		debug_printf("ssi: About to add a permit\n");
		if (od->sess->ssi.received_data)
			aim_ssi_addpord(od->sess, od->conn, &who, 1, AIM_SSI_TYPE_PERMIT);
	}
}

static void oscar_add_deny(struct gaim_connection *gc, char *who) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (od->icq) {
		if (gc->permdeny != 4) return;
		oscar_set_permit_deny(gc);
	} else {
		debug_printf("ssi: About to add a deny\n");
		if (od->sess->ssi.received_data)
			aim_ssi_addpord(od->sess, od->conn, &who, 1, AIM_SSI_TYPE_DENY);
	}
}

static void oscar_rem_permit(struct gaim_connection *gc, char *who) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (od->icq) {
		if (gc->permdeny != 3) return;
		oscar_set_permit_deny(gc);
	} else {
		debug_printf("ssi: About to delete a permit\n");
		if (od->sess->ssi.received_data)
			aim_ssi_delpord(od->sess, od->conn, &who, 1, AIM_SSI_TYPE_PERMIT);
	}
}

static void oscar_rem_deny(struct gaim_connection *gc, char *who) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (od->icq) {
		if (gc->permdeny != 4) return;
		oscar_set_permit_deny(gc);
	} else {
		debug_printf("ssi: About to delete a deny\n");
		if (od->sess->ssi.received_data)
			aim_ssi_delpord(od->sess, od->conn, &who, 1, AIM_SSI_TYPE_DENY);
	}
}

static GList *oscar_away_states(struct gaim_connection *gc)
{
	struct oscar_data *od = gc->proto_data;
	GList *m = NULL;

	if (!od->icq)
		return g_list_append(m, GAIM_AWAY_CUSTOM);

	m = g_list_append(m, "Online");
	m = g_list_append(m, "Away");
	m = g_list_append(m, "Do Not Disturb");
	m = g_list_append(m, "Not Available");
	m = g_list_append(m, "Occupied");
	m = g_list_append(m, "Free For Chat");
	m = g_list_append(m, "Invisible");

	return m;
}

static void oscar_change_email(struct gaim_connection *gc, char *email)
{
	struct oscar_data *od = gc->proto_data;
	aim_conn_t *conn = aim_getconn_type(od->sess, AIM_CONN_TYPE_AUTH);

	if (conn) {
		aim_admin_setemail(od->sess, conn, email);
	} else {
		od->setemail = TRUE;
		od->email = g_strdup(email);
		aim_reqservice(od->sess, od->conn, AIM_CONN_TYPE_AUTH);
	}
}

static void oscar_format_screenname(struct gaim_connection *gc, char *nick) {
	struct oscar_data *od = gc->proto_data;
	if (!strcmp(normalize(nick), normalize(gc->username))) {
		if (!aim_getconn_type(od->sess, AIM_CONN_TYPE_AUTH)) {
			od->setnick = TRUE;
			od->newsn = g_strdup(nick);
			aim_reqservice(od->sess, od->conn, AIM_CONN_TYPE_AUTH);
		} else {
			aim_admin_setnick(od->sess, aim_getconn_type(od->sess, AIM_CONN_TYPE_AUTH), nick);
		}
	} else {
		do_error_dialog("The new formatting is invalid.",
				"Screenname formatting can change only capitalization and whitespace.", GAIM_ERROR);
	}
}

static void oscar_do_action(struct gaim_connection *gc, char *act)
{
	struct oscar_data *od = gc->proto_data;
	aim_conn_t *conn = aim_getconn_type(od->sess, AIM_CONN_TYPE_AUTH);

	if (!strcmp(act, "Set User Info")) {
		show_set_info(gc);
	} else if (!strcmp(act, "Change Password")) {
		show_change_passwd(gc);
	} else if (!strcmp(act, "Format Screenname")) {
		do_prompt_dialog("New screenname formatting:", 
				 gc->displayname, gc, oscar_format_screenname, NULL);
	} else if (!strcmp(act, "Confirm Account")) {
		if (!conn) {
			od->conf = TRUE;
			aim_reqservice(od->sess, od->conn, AIM_CONN_TYPE_AUTH);
		} else
			aim_admin_reqconfirm(od->sess, conn);
	} else if (!strcmp(act, "Display Current Registered Address")) {
		if (!conn) {
			od->reqemail = TRUE;
			aim_reqservice(od->sess, od->conn, AIM_CONN_TYPE_AUTH);
		} else
			aim_admin_getinfo(od->sess, conn, 0x11);
	} else if (!strcmp(act, "Change Current Registered Address")) {
		do_prompt_dialog("Change Address To: ", NULL, gc, oscar_change_email, NULL);
	} else if (!strcmp(act, "Search for Buddy by Email")) {
		show_find_email(gc);
	}
}

static GList *oscar_actions()
{
	GList *m = NULL;

	m = g_list_append(m, "Set User Info");
	m = g_list_append(m, NULL);
	m = g_list_append(m, "Change Password");
	m = g_list_append(m, "Format Screenname");
	m = g_list_append(m, "Confirm Account");
	m = g_list_append(m, "Display Current Registered Address");
	m = g_list_append(m, "Change Current Registered Address");
	m = g_list_append(m, NULL);
	m = g_list_append(m, "Search for Buddy by Email");

	return m;
}

static void oscar_change_passwd(struct gaim_connection *gc, char *old, char *new)
{
	struct oscar_data *od = gc->proto_data;
	if (!aim_getconn_type(od->sess, AIM_CONN_TYPE_AUTH)) {
		od->chpass = TRUE;
		od->oldp = g_strdup(old);
		od->newp = g_strdup(new);
		aim_reqservice(od->sess, od->conn, AIM_CONN_TYPE_AUTH);
	} else {
		aim_admin_changepasswd(od->sess, aim_getconn_type(od->sess, AIM_CONN_TYPE_AUTH),
				new, old);
	}
}

static void oscar_convo_closed(struct gaim_connection *gc, char *who)
{
	struct oscar_data *od = gc->proto_data;
	struct direct_im *dim = find_direct_im(od, who);

	if (!dim)
		return;

	od->direct_ims = g_slist_remove(od->direct_ims, dim);
	gaim_input_remove(dim->watcher);
	aim_conn_kill(od->sess, &dim->conn);
	g_free(dim);
}



static struct prpl *my_protocol = NULL;

void oscar_init(struct prpl *ret) {
	ret->protocol = PROTO_OSCAR;
	ret->options = OPT_PROTO_BUDDY_ICON | OPT_PROTO_IM_IMAGE;
	ret->name = oscar_name;
	ret->list_icon = oscar_list_icon;
	ret->away_states = oscar_away_states;
	ret->actions = oscar_actions;
	ret->do_action = oscar_do_action;
	ret->buddy_menu = oscar_buddy_menu;
	ret->edit_buddy_menu = oscar_edit_buddy_menu;
	ret->user_opts = oscar_user_opts;
	ret->login = oscar_login;
	ret->close = oscar_close;
	ret->send_im = oscar_send_im;
	ret->send_typing = oscar_send_typing;
	ret->set_info = oscar_set_info;
	ret->get_info = oscar_get_info;
	ret->set_away = oscar_set_away;
	ret->get_away = oscar_get_away;
	ret->set_dir = oscar_set_dir;
	ret->get_dir = NULL; /* Oscar really doesn't have this */
	ret->dir_search = oscar_dir_search;
	ret->set_idle = oscar_set_idle;
	ret->change_passwd = oscar_change_passwd;
	ret->add_buddy = oscar_add_buddy;
	ret->add_buddies = oscar_add_buddies;
	ret->group_buddy = oscar_move_buddy;
	ret->rename_group = oscar_rename_group;
	ret->remove_buddy = oscar_remove_buddy;
	ret->remove_buddies = oscar_remove_buddies;
	ret->add_permit = oscar_add_permit;
	ret->add_deny = oscar_add_deny;
	ret->rem_permit = oscar_rem_permit;
	ret->rem_deny = oscar_rem_deny;
	ret->set_permit_deny = oscar_set_permit_deny;
	ret->warn = oscar_warn;
	ret->chat_info = oscar_chat_info;
	ret->join_chat = oscar_join_chat;
	ret->chat_invite = oscar_chat_invite;
	ret->chat_leave = oscar_chat_leave;
	ret->chat_whisper = NULL;
	ret->chat_send = oscar_chat_send;
	ret->keepalive = oscar_keepalive;
	ret->convo_closed = oscar_convo_closed;

	my_protocol = ret;
}

#ifndef STATIC

char *gaim_plugin_init(GModule *handle)
{
	load_protocol(oscar_init, sizeof(struct prpl));
	return NULL;
}

void gaim_plugin_remove()
{
	struct prpl *p = find_prpl(PROTO_OSCAR);
	if (p == my_protocol)
		unload_protocol(p);
}

char *name()
{
	return "Oscar";
}

char *description()
{
	return PRPL_DESC("Oscar");
}

#endif
