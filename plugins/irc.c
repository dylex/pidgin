/*
 * gaim - IRC Protocol Plugin
 *
 * Copyright (C) 2000, Rob Flynn <rob@tgflinux.com>
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
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
#include "../config.h"
#endif


#include <netdb.h>
#include <gtk/gtk.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#include "gnome_applet_mgr.h"

#include "pixmaps/cancel.xpm"
#include "pixmaps/ok.xpm"

/* FIXME: We shouldn't have hard coded servers and ports :-) */
#define IRC_SERVER "irc.mozilla.org"
#define IRC_PORT 6667

#define IRC_BUF_LEN 4096

static int chat_id = 0;

struct irc_channel { 
	int id;
	gchar *name;
};

struct irc_data {
	int fd;

	GList *channels;
};

static char *irc_name() {
	return "IRC";
}

char *name() {
	return "IRC";
}

char *description() {
	return "Allows gaim to use the IRC protocol";
}

void irc_join_chat( struct gaim_connection *gc, int id, char *name) {
	struct irc_data *idata = (struct irc_data *)gc->proto_data;
	gchar *buf = (gchar *)g_malloc(IRC_BUF_LEN+1);

	g_snprintf(buf, IRC_BUF_LEN, "JOIN %s\n", name);
	write(idata->fd, buf, strlen(buf));
	
	g_free(buf);
}

void irc_send_im( struct gaim_connection *gc, char *who, char *message, int away) {

	struct irc_data *idata = (struct irc_data *)gc->proto_data;
	gchar *buf = (gchar *)g_malloc(IRC_BUF_LEN + 1);

	/* Before we actually send this, we should check to see if they're trying
	 * To issue a /me command and handle it properly. */

	if ( (g_strncasecmp(message, "/me ", 4) == 0) && (strlen(message)>4)) {
		/* We have /me!! We have /me!! :-) */

		gchar *temp = (gchar *)g_malloc(IRC_BUF_LEN+1);
		strcpy(temp, message+4);
		g_snprintf(buf, IRC_BUF_LEN, "PRIVMSG %s :%cACTION %s%c\n", who, '\001', temp, '\001');
		g_free(temp);
	}
	else
	{
		g_snprintf(buf, IRC_BUF_LEN, "PRIVMSG %s :%s\n", who, message);
	}

	write(idata->fd, buf, strlen(buf)); 

	g_free(buf);	
}

int find_id_by_name(struct gaim_connection *gc, char *name) {
	gchar *temp = (gchar *)g_malloc(IRC_BUF_LEN + 1);
	GList *templist;
	struct irc_channel *channel;

	templist = ((struct irc_data *)gc->proto_data)->channels;

	while (templist) {
		channel = (struct irc_channel *)templist->data;

		g_snprintf(temp, IRC_BUF_LEN, "#%s", channel->name);

		if (g_strcasecmp(temp, name) == 0) {
			g_free(temp);
			return channel->id;
		}

		templist = templist -> next;
	}

	g_free(temp);

	/* Return -1 if we have no ID */
	return -1;
}

struct irc_channel * find_channel_by_name(struct gaim_connection *gc, char *name) {
	gchar *temp = (gchar *)g_malloc(IRC_BUF_LEN + 1);
	GList *templist;
	struct irc_channel *channel;

	templist = ((struct irc_data *)gc->proto_data)->channels;

	while (templist) {
		channel = (struct irc_channel *)templist->data;

		g_snprintf(temp, IRC_BUF_LEN, "%s", channel->name);

		if (g_strcasecmp(temp, name) == 0) {
			g_free(temp);
			return channel;
		}

		templist = templist -> next;
	}

	g_free(temp);

	/* If we found nothing, return nothing :-) */
	return NULL;
}

struct irc_channel * find_channel_by_id (struct gaim_connection *gc, int id) {
	struct irc_data *idata = (struct irc_data *)gc->proto_data;
	struct irc_channel *channel;
	
	GList *temp;

	temp = idata->channels;

	while (temp) {
		channel = (struct irc_channel *)temp->data;

		if (channel->id == id) {
			/* We've found our man */
			return channel;
		}
		
		temp = temp->next;
	}


	/* If we didnt find one, return NULL */
	return NULL;
}

void irc_chat_send( struct gaim_connection *gc, int id, char *message) {

	struct irc_data *idata = (struct irc_data *)gc->proto_data;
	struct irc_channel *channel = g_new0(struct irc_channel, 1);
	gchar *buf = (gchar *)g_malloc(IRC_BUF_LEN + 1);

	/* First lets get our current channel */
	channel = find_channel_by_id(gc, id);
	

	if (!channel) {
		/* If for some reason we've lost our channel, let's bolt */
		return;
	}
	
	
	/* Before we actually send this, we should check to see if they're trying
	 * To issue a /me command and handle it properly. */

	if ( (g_strncasecmp(message, "/me ", 4) == 0) && (strlen(message)>4)) {
		/* We have /me!! We have /me!! :-) */

		gchar *temp = (gchar *)g_malloc(IRC_BUF_LEN+1);
		strcpy(temp, message+4);
		g_snprintf(buf, IRC_BUF_LEN, "PRIVMSG #%s :%cACTION %s%c\n", channel->name, '\001', temp, '\001');
		g_free(temp);
	}
	else
	{
		g_snprintf(buf, IRC_BUF_LEN, "PRIVMSG #%s :%s\n", channel->name, message);
	}

	write(idata->fd, buf, strlen(buf)); 

	/* Since AIM expects us to receive the message we send, we gotta fake it */
	serv_got_chat_in(gc, id, gc->username, 0, message);

	g_free(buf);	
}

void irc_callback ( struct gaim_connection * gc ) {

	int i = 0;
	char c;
	gchar buf[4096];
	gchar **buf2;
	int status;
	struct irc_data *idata;

	idata = (struct irc_data *)gc->proto_data;
	
	do {
		status = recv(idata->fd, &c, 1, 0);

		if (!status)
		{
			exit(1);
		}
		buf[i] = c;
		i++;
	} while (c != '\n');

	buf[i] = '\0';

	/* And remove that damned trailing \n */
	g_strchomp(buf);

	/* For now, lets display everything to the console too. Im such a bitch */
	printf("IRC:'%'s\n", buf);

	if ( (strstr(buf, " JOIN ")) && (buf[0] == ':') && (!strstr(buf, " NOTICE "))) {

		gchar u_channel[128];
		struct irc_channel *channel;
		int id;
		int j;

		for (j = 0, i = 1; buf[i] != '#'; j++, i++) {
		}
		
		i++;
	
		strcpy(u_channel, buf+i);

		/* Looks like we're going to join the channel for real now.  Let's create
		 * a valid channel structure and add it to our list.  Let's make sure that
		 * we are not already in a channel first */

		channel = find_channel_by_name(gc, u_channel);

		if (!channel) {
			chat_id++;

			channel = g_new0(struct irc_channel, 1);
			
			channel->id = chat_id;
			channel->name = strdup(u_channel);
	
			idata->channels = g_list_append(idata->channels, channel);

			serv_got_joined_chat(gc, chat_id, u_channel);	
			printf("IIII: I joined '%s' with a strlen() of '%d'\n", u_channel, strlen(u_channel));
		} else {
			/* Someone else joined. */
		}

		return;
	}
	
	if ( (strstr(buf, " PART ")) && (buf[0] == ':') && (!strstr(buf, " NOTICE "))) {

		gchar u_channel[128];
		gchar u_nick[128];

		struct irc_channel *channel = g_new0(struct irc_channel, 1);
		int id;
		int j;
		GList *test = NULL;

		for (j = 0, i = 1; buf[i] != '!'; j++, i++) {
			u_nick[j] = buf[i];
		}
		u_nick[j] = '\0';

		i++;
		
		for (j = 0; buf[i] != '#'; j++, i++) {
		}
		
		i++;
		
		strcpy(u_channel, buf+i);


		/* Now, lets check to see if it was US that was leaving.  If so, do the
		 * correct thing by closing up all of our old channel stuff. Otherwise,
		 * we should just print that someone left */

		if (g_strcasecmp(u_nick, gc->username) == 0) {
	
			/* Looks like we're going to join the channel for real now.  Let's create
			 * a valid channel structure and add it to our list */

			channel = find_channel_by_name(gc, u_channel);

			if (!channel) {
				return;
			}

			serv_got_chat_left(gc, channel->id);

			idata->channels = g_list_remove(idata->channels, channel);
			g_free(channel);	
			return;
		}

		/* Otherwise, lets just say someone left */
		printf("%s has left #%s\n", u_nick, u_channel);

		return;
	}
	
	if ( (strstr(buf, "PRIVMSG ")) && (buf[0] == ':')) {
		gchar u_nick[128];
		gchar u_host[255];
		gchar u_command[32];
		gchar u_channel[128];
		gchar u_message[IRC_BUF_LEN];
		int j;
		int msgcode = 0;

		for (j = 0, i = 1; buf[i] != '!'; j++, i++) {
			u_nick[j] = buf[i];
		}

		u_nick[j] = '\0'; i++;

		for (j = 0; buf[i] != ' '; j++, i++) {
			u_host[j] = buf[i];
		}

		u_host[j] = '\0'; i++;

		for (j = 0; buf[i] != ' '; j++, i++) {
			u_command[j] = buf[i];
		}

		u_command[j] = '\0'; i++;

		for (j = 0; buf[i] != ':'; j++, i++) {
			u_channel[j] = buf[i];
		}

		u_channel[j-1] = '\0'; i++;


		/* Now that everything is parsed, the rest of this baby must be our message */
		strncpy(u_message, buf + i, IRC_BUF_LEN);
	
		/* Now, lets check the message to see if there's anything special in it */
		if (u_message[0] == '\001') {
			if (g_strncasecmp(u_message, "\001ACTION ", 8) == 0) {
				/* Looks like we have an action. Let's parse it a little */
				strcpy(buf, u_message);

				strcpy(u_message, "/me ");
				for (j = 4, i = 8; buf[i] != '\001'; i++, j++) {
					u_message[j] = buf[i];
				}
				u_message[j] = '\0';
			}
		}


		/* Let's check to see if we have a channel on our hands */
		if (u_channel[0] == '#') {
			/* Yup.  We have a channel */
			int id;

			id = find_id_by_name(gc, u_channel);
			if (id != -1) {
				serv_got_chat_in(gc, id, u_nick, 0, u_message);
			}
		}
		else {
			/* Nope. Let's treat it as a private message */
			printf("JUST GOT AN IM!!\n");
			serv_got_im(gc, u_nick, u_message, 0);
		}

		return;
	}

	/* Let's parse PING requests so that we wont get booted for inactivity */

	if (strncmp(buf, "PING :", 6) == 0) {
		buf2 = g_strsplit(buf, ":", 1);
		
		/* Let's build a new response */
		g_snprintf(buf, IRC_BUF_LEN, "PONG :%s\n", buf2[1]);
		write(idata->fd, buf, strlen(buf));

		/* And clean up after ourselves */
		g_strfreev(buf2);

		return;
	}

}

void irc_handler(gpointer data, gint source, GdkInputCondition condition) {
	irc_callback(data);
}

void irc_close(struct gaim_connection *gc) {
	struct irc_data *idata = (struct irc_data *)gc->proto_data;
	gchar *buf = (gchar *)g_malloc(IRC_BUF_LEN);

	g_snprintf(buf, IRC_BUF_LEN, "QUIT :GAIM [www.marko.net/gaim]\n");
	write(idata->fd, buf, strlen(buf));

	g_free(buf);
	close(idata->fd);
	g_free(gc->proto_data);
}

void irc_chat_leave(struct gaim_connection *gc, int id) {
	struct irc_data *idata = (struct irc_data *)gc->proto_data;
	struct irc_channel *channel;
	gchar *buf = (gchar *)g_malloc(IRC_BUF_LEN+1);
	
	channel = find_channel_by_id(gc, id);
	
	if (!channel) {
		return;
	}

	g_snprintf(buf, IRC_BUF_LEN, "PART #%s\n", channel->name);
	write(idata->fd, buf, strlen(buf));

	g_free(buf);
}

void irc_login(struct aim_user *user) {
	int fd;
	struct hostent *host;
	struct sockaddr_in site;
	char buf[4096];
	
	struct gaim_connection *gc = new_gaim_conn(PROTO_IRC, user->username, user->password);
	struct irc_data *idata = gc->proto_data = g_new0(struct irc_data, 1);
	char c;
	int i;
	int status;

	set_login_progress(gc, 1, buf);

	while (gtk_events_pending())
		gtk_main_iteration();

	host = gethostbyname(IRC_SERVER);
	if (!host) {
		hide_login_progress(gc, "Unable to resolve hostname");
		destroy_gaim_conn(gc);
		return;
	}

	site.sin_family = AF_INET;
	site.sin_addr.s_addr = *(long *)(host->h_addr);
	site.sin_port = htons(IRC_PORT);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		hide_login_progress(gc, "Unable to create socket");
		destroy_gaim_conn(gc);
		return;
	}

	if (connect(fd, (struct sockaddr *)&site, sizeof(site)) < 0) {
		hide_login_progress(gc, "Unable to connect.");
		destroy_gaim_conn(gc);
		return;
	}

	idata->fd = fd;
	
	g_snprintf(buf, sizeof(buf), "Signon: %s", gc->username);
	set_login_progress(gc, 2, buf);

	/* This is where we will attempt to sign on */
	
	/* FIXME: This should be their servername, not their username. im just lazy right now */

	g_snprintf(buf, 4096, "NICK %s\nUSER %s localhost %s :GAIM (www.marko.net/gaim)\n", gc->username, gc->username, gc->username);
	write(idata->fd, buf, strlen(buf));


	/* Now lets sign ourselves on */
        account_online(gc);

	if (mainwindow)
		gtk_widget_hide(mainwindow);
	
	show_buddy_list();
	refresh_buddy_window();
	
	serv_finish_login(gc);
	gaim_setup(gc);

	gc->inpa = gdk_input_add(idata->fd, GDK_INPUT_READ, irc_handler, gc);
}

struct prpl *irc_init() {
	struct prpl *ret = g_new0(struct prpl, 1);

	ret->protocol = PROTO_IRC;
	ret->name = irc_name;
	ret->login = irc_login;
	ret->close = irc_close;
	ret->send_im = irc_send_im;
	ret->set_info = NULL;
	ret->get_info = NULL;
	ret->set_away = NULL;
	ret->get_away_msg = NULL;
	ret->set_dir = NULL;
	ret->get_dir = NULL;
	ret->dir_search = NULL;
	ret->set_idle = NULL;
	ret->change_passwd = NULL;
	ret->add_buddy = NULL;
	ret->add_buddies = NULL;
	ret->remove_buddy = NULL;
	ret->add_permit = NULL;
	ret->add_deny = NULL;
	ret->warn = NULL;
	ret->accept_chat = NULL;
	ret->join_chat = irc_join_chat;
	ret->chat_invite = NULL;
	ret->chat_leave = irc_chat_leave;
	ret->chat_whisper = NULL;
	ret->chat_send = irc_chat_send;
	ret->keepalive = NULL;
	
	return ret;
}

int gaim_plugin_init(void *handle) {
	protocols = g_slist_append(protocols, irc_init());
	return 0;
}
