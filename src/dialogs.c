/*
 * gaim
 *
 * Gaim is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
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
#include "gtkinternal.h"

#include "debug.h"
#include "log.h"
#include "multi.h"
#include "notify.h"
#include "prefs.h"
#include "prpl.h"
#include "request.h"
#include "status.h"
#include "util.h"

#include "gtkblist.h"
#include "gtkconv.h"
#include "gtkimhtml.h"
#include "gtkimhtmltoolbar.h"
#include "gtkprefs.h"
#include "gtkutils.h"
#include "stock.h"

#include "ui.h"

/* XXX */
#include "gaim.h"

static GList *dialogwindows = NULL;
static GtkWidget *fontseld = NULL;

struct confirm_del {
	GtkWidget *window;
	GtkWidget *label;
	GtkWidget *ok;
	GtkWidget *cancel;
	char name[1024];
	GaimConnection *gc;
};

struct create_away {
	GtkWidget *window;
	GtkWidget *toolbar;
	GtkWidget *entry;
	GtkWidget *text;
	struct away_message *mess;
};

struct warning {
	GtkWidget *window;
	GtkWidget *anon;
	char *who;
	GaimConnection *gc;
};

struct getuserinfo {
	GtkWidget *window;
	GtkWidget *entry;
	GtkWidget *account;
	GaimConnection *gc;
};

struct view_log {
	long offset;
	int options;
	char *name;
	GtkWidget *bbox;
	GtkWidget *window;
	GtkWidget *layout;
	void *clear_handle;
};

/* Wrapper to get all the text from a GtkTextView */
gchar* gtk_text_view_get_text(GtkTextView *text, gboolean include_hidden_chars)
{
	GtkTextBuffer *buffer;
	GtkTextIter start, end;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
	gtk_text_buffer_get_start_iter(buffer, &start);
	gtk_text_buffer_get_end_iter(buffer, &end);

	return gtk_text_buffer_get_text(buffer, &start, &end, include_hidden_chars);
}

/*------------------------------------------------------------------------*/
/*  Destroys                                                              */
/*------------------------------------------------------------------------*/

static void destroy_dialog(GtkWidget *w, GtkWidget *w2)
{
	GtkWidget *dest;

	if (!GTK_IS_WIDGET(w2))
		dest = w;
	else
		dest = w2;

	dialogwindows = g_list_remove(dialogwindows, dest);
	gtk_widget_destroy(dest);
}

void destroy_all_dialogs()
{
	while (dialogwindows)
		destroy_dialog(NULL, dialogwindows->data);

	if (awaymessage)
		do_im_back(NULL, NULL);
}

static void do_warn(GtkWidget *widget, gint resp, struct warning *w)
{
	if (resp == GTK_RESPONSE_OK)
		serv_warn(w->gc, w->who, (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->anon))) ? 1 : 0);

	destroy_dialog(NULL, w->window);
	g_free(w->who);
	g_free(w);
}

void show_warn_dialog(GaimConnection *gc, const char *who)
{
	char *labeltext;
	GtkWidget *hbox, *vbox;
	GtkWidget *label;
	GtkWidget *img = gtk_image_new_from_stock(GAIM_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_DIALOG);
	GaimConversation *c = gaim_find_conversation_with_account(who, gc->account);

	struct warning *w = g_new0(struct warning, 1);
	w->who = g_strdup(who);
	w->gc = gc;

	gtk_misc_set_alignment(GTK_MISC(img), 0, 0);

	w->window = gtk_dialog_new_with_buttons(_("Warn User"),
			GTK_WINDOW(GAIM_GTK_WINDOW(c->window)->window), 0,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GAIM_STOCK_WARN, GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response (GTK_DIALOG(w->window), GTK_RESPONSE_OK);
	g_signal_connect(G_OBJECT(w->window), "response", G_CALLBACK(do_warn), w);

	gtk_container_set_border_width (GTK_CONTAINER(w->window), 6);
	gtk_window_set_resizable(GTK_WINDOW(w->window), FALSE);
	gtk_dialog_set_has_separator(GTK_DIALOG(w->window), FALSE);
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(w->window)->vbox), 12);
	gtk_container_set_border_width (GTK_CONTAINER(GTK_DIALOG(w->window)->vbox), 6);

	hbox = gtk_hbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(w->window)->vbox), hbox);
	gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(hbox), vbox);
	labeltext = g_strdup_printf(_("<span weight=\"bold\" size=\"larger\">Warn %s?</span>\n\n"
				      "This will increase %s's warning level and he or she will be subject to harsher rate limiting.\n"), who, who);
	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), labeltext);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	g_free(labeltext);

	w->anon = gtk_check_button_new_with_mnemonic(_("Warn _anonymously?"));
	gtk_box_pack_start(GTK_BOX(vbox), w->anon, FALSE, FALSE, 0);

	hbox = gtk_hbox_new(FALSE, 6);
	gtk_container_add(GTK_CONTAINER(vbox), hbox);
	img = gtk_image_new_from_stock(GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_MENU);
	gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);
	labeltext = _("<b>Anonymous warnings are less severe.</b>");
	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), labeltext);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	dialogwindows = g_list_prepend(dialogwindows, w->window);
	gtk_widget_show_all(w->window);
}

static void
do_remove_chat(GaimChat *chat)
{
	gaim_blist_remove_chat(chat);
	gaim_blist_save();
}

static void
do_remove_buddy(GaimBuddy *b)
{
	GaimGroup *g;
	GaimConversation *c;
	gchar *name;
	GaimAccount *account;

	if (!b)
		return;

	g = gaim_find_buddys_group(b);
	name = g_strdup(b->name); /* b->name is a crasher after remove_buddy */
	account = b->account;

	gaim_debug(GAIM_DEBUG_INFO, "blist",
			   "Removing '%s' from buddy list.\n", b->name);
	serv_remove_buddy(b->account->gc, name, g->name);
	gaim_blist_remove_buddy(b);
	gaim_blist_save();

	c = gaim_find_conversation_with_account(name, account);

	if (c != NULL)
		gaim_conversation_update(c, GAIM_CONV_UPDATE_REMOVE);

	g_free(name);
}

static void do_remove_contact(GaimContact *c)
{
	GaimBlistNode *bnode, *cnode;
	GaimGroup *g;

	if(!c)
		return;

	cnode = (GaimBlistNode *)c;
	g = (GaimGroup*)cnode->parent;
	for(bnode = cnode->child; bnode; bnode = bnode->next) {
		GaimBuddy *b = (GaimBuddy*)bnode;
		if(b->account->gc)
			serv_remove_buddy(b->account->gc, b->name, g->name);
	}
	gaim_blist_remove_contact(c);
}

void do_remove_group(GaimGroup *g)
{
	GaimBlistNode *cnode, *bnode;

	cnode = ((GaimBlistNode*)g)->child;

	while(cnode) {
		if(GAIM_BLIST_NODE_IS_CONTACT(cnode)) {
			bnode = cnode->child;
			cnode = cnode->next;
			while(bnode) {
				GaimBuddy *b;
				if(GAIM_BLIST_NODE_IS_BUDDY(bnode)) {
					GaimConversation *c;
                                        b = (GaimBuddy*)bnode;
					bnode = bnode->next;
					c = gaim_find_conversation_with_account(b->name, b->account);
					if(gaim_account_is_connected(b->account)) {
						serv_remove_buddy(b->account->gc, b->name, g->name);
						gaim_blist_remove_buddy(b);
						if(c)
							gaim_conversation_update(c,
									GAIM_CONV_UPDATE_REMOVE);
					}
				} else {
					bnode = bnode->next;
				}
			}
		} else if(GAIM_BLIST_NODE_IS_CHAT(cnode)) {
			GaimChat *chat = (GaimChat *)cnode;
			cnode = cnode->next;
			if(gaim_account_is_connected(chat->account))
				gaim_blist_remove_chat(chat);
		} else {
			cnode = cnode->next;
		}
	}

	gaim_blist_remove_group(g);
	gaim_blist_save();
}

void show_confirm_del(GaimBuddy *b)
{
	char *text;
	if (!b)
		return;

	text = g_strdup_printf(_("You are about to remove %s from your buddy list.  Do you want to continue?"), b->name);

	gaim_request_action(NULL, NULL, _("Remove Buddy"), text, -1, b, 2,
						_("Remove Buddy"), G_CALLBACK(do_remove_buddy),
						_("Cancel"), NULL);

	g_free(text);
}

void show_confirm_del_blist_chat(GaimChat *chat)
{
	char *name = gaim_chat_get_display_name(chat);
	char *text = g_strdup_printf(_("You are about to remove the chat %s from your buddy list.  Do you want to continue?"), name);

	gaim_request_action(NULL, NULL, _("Remove Chat"), text, -1, chat, 2,
						_("Remove Chat"), G_CALLBACK(do_remove_chat),
						_("Cancel"), NULL);

	g_free(name);
	g_free(text);
}

void show_confirm_del_group(GaimGroup *g)
{
	char *text = g_strdup_printf(_("You are about to remove the group %s and all its members from your buddy list.  Do you want to continue?"),
			       g->name);

	gaim_request_action(NULL, NULL, _("Remove Group"), text, -1, g, 2,
						_("Remove Group"), G_CALLBACK(do_remove_group),
						_("Cancel"), NULL);

	g_free(text);
}

void show_confirm_del_contact(GaimContact *c)
{
	GaimBuddy *b = gaim_contact_get_priority_buddy(c);

	if(!b)
		return;

	if(((GaimBlistNode*)c)->child == (GaimBlistNode*)b &&
			!((GaimBlistNode*)b)->next) {
		show_confirm_del(b);
	} else {
		char *text = g_strdup_printf(_("You are about to remove the contact containing %s and %d other buddies from your buddy list.  Do you want to continue?"),
			       b->name, c->totalsize - 1);

		gaim_request_action(NULL, NULL, _("Remove Contact"), text, -1, c, 2,
				_("Remove Contact"), G_CALLBACK(do_remove_contact),
				_("Cancel"), NULL);

		g_free(text);
	}
}

static gboolean show_ee_dialog(const char *ee)
{
	GtkWidget *window;
	GtkWidget *hbox;
	GtkWidget *label;
	GaimGtkBuddyList *gtkblist;
	GtkWidget *img = gtk_image_new_from_stock(GAIM_STOCK_DIALOG_COOL, GTK_ICON_SIZE_DIALOG);
	gchar *norm = gaim_strreplace(ee, "rocksmyworld", "");

	gtkblist = GAIM_GTK_BLIST(gaim_get_blist());

	label = gtk_label_new(NULL);
	if (!strcmp(norm, "zilding"))
		gtk_label_set_markup(GTK_LABEL(label),
				     "<span weight=\"bold\" size=\"large\" foreground=\"purple\">Amazing!  Simply Amazing!</span>");
	else if (!strcmp(norm, "robflynn"))
		gtk_label_set_markup(GTK_LABEL(label),
				     "<span weight=\"bold\" size=\"large\" foreground=\"#1f6bad\">Pimpin\' Penguin Style! *Waddle Waddle*</span>");
	else if (!strcmp(norm, "flynorange"))
		gtk_label_set_markup(GTK_LABEL(label),
				      "<span weight=\"bold\" size=\"large\" foreground=\"blue\">You should be me.  I'm so cute!</span>");
	else if (!strcmp(norm, "ewarmenhoven"))
		gtk_label_set_markup(GTK_LABEL(label),
				     "<span weight=\"bold\" size=\"large\" foreground=\"orange\">Now that's what I like!</span>");
	else if (!strcmp(norm, "markster97"))
		gtk_label_set_markup(GTK_LABEL(label),
				     "<span weight=\"bold\" size=\"large\" foreground=\"brown\">Ahh, and excellent choice!</span>");
	else if (!strcmp(norm, "seanegn"))
		gtk_label_set_markup(GTK_LABEL(label),
				     "<span weight=\"bold\" size=\"large\" foreground=\"#009900\">Everytime you click my name, an angel gets its wings.</span>");
	else if (!strcmp(norm, "chipx86"))
		gtk_label_set_markup(GTK_LABEL(label),
				     "<span weight=\"bold\" size=\"large\" foreground=\"red\">This sunflower seed taste like pizza.</span>");
	else if (!strcmp(norm, "markdoliner"))
		gtk_label_set_markup(GTK_LABEL(label),
				     "<span weight=\"bold\" size=\"large\" foreground=\"#6364B1\">Hey!  I was in that tumbleweed!</span>");
	else if (!strcmp(norm, "lschiere"))
		gtk_label_set_markup(GTK_LABEL(label),
				     "<span weight=\"bold\" size=\"large\" foreground=\"gray\">I'm not anything.</span>");
	g_free(norm);

	if (strlen(gtk_label_get_label(GTK_LABEL(label))) <= 0)
		return FALSE;

	window = gtk_dialog_new_with_buttons(GAIM_ALERT_TITLE, GTK_WINDOW(gtkblist->window), 0, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response (GTK_DIALOG(window), GTK_RESPONSE_OK);
	g_signal_connect(G_OBJECT(window), "response", G_CALLBACK(gtk_widget_destroy), NULL);

	gtk_container_set_border_width (GTK_CONTAINER(window), 6);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_dialog_set_has_separator(GTK_DIALOG(window), FALSE);
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(window)->vbox), 12);
	gtk_container_set_border_width (GTK_CONTAINER(GTK_DIALOG(window)->vbox), 6);

	hbox = gtk_hbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(window)->vbox), hbox);
	gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);

	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	gtk_widget_show_all(window);
	return TRUE;
}

static void
new_im_cb(gpointer data, GaimRequestFields *fields)
{
	const char *username;
	GaimAccount *account;
	GaimConversation *conv;

	username = gaim_request_fields_get_string(fields,  "screenname");
	account  = gaim_request_fields_get_account(fields, "account");

	conv = gaim_find_conversation_with_account(username, account);

	if (conv == NULL)
		conv = gaim_conversation_new(GAIM_CONV_IM, account, username);
	else
		gaim_conv_window_raise(gaim_conversation_get_window(conv));
}

void
show_im_dialog(void)
{
	GaimRequestFields *fields;
	GaimRequestFieldGroup *group;
	GaimRequestField *field;

	fields = gaim_request_fields_new();

	group = gaim_request_field_group_new(NULL);
	gaim_request_fields_add_group(fields, group);

	field = gaim_request_field_string_new("screenname", _("_Screen name"),
										  NULL, FALSE);
	gaim_request_field_set_required(field, TRUE);
	gaim_request_field_set_type_hint(field, "screenname");
	gaim_request_field_group_add_field(group, field);

	field = gaim_request_field_account_new("account", _("_Account"), NULL);
	gaim_request_field_set_visible(field,
		(gaim_connections_get_all() != NULL &&
		 gaim_connections_get_all()->next != NULL));
	gaim_request_field_set_required(field, TRUE);
	gaim_request_field_group_add_field(group, field);

	gaim_request_fields(gaim_get_blist(), _("New Instant Message"),
						NULL,
						_("Please enter the screen name of the person you "
						  "would like to IM."),
						fields,
						_("OK"), G_CALLBACK(new_im_cb),
						_("Cancel"), NULL,
						NULL);
}

static void
get_info_cb(gpointer data, GaimRequestFields *fields)
{
	char *username;
	gboolean found = FALSE;
	GaimAccount *account;

	account  = gaim_request_fields_get_account(fields, "account");

	username = g_strdup(gaim_normalize(account,
		gaim_request_fields_get_string(fields,  "screenname")));

	if (username != NULL && gaim_str_has_suffix(username, "rocksmyworld"))
		found = show_ee_dialog(username);

	if (!found && username != NULL && *username != '\0' && account != NULL)
		serv_get_info(gaim_account_get_connection(account), username);

	g_free(username);
}

void
show_info_dialog(void)
{
	GaimRequestFields *fields;
	GaimRequestFieldGroup *group;
	GaimRequestField *field;

	fields = gaim_request_fields_new();

	group = gaim_request_field_group_new(NULL);
	gaim_request_fields_add_group(fields, group);

	field = gaim_request_field_string_new("screenname", _("_Screen name"),
										  NULL, FALSE);
	gaim_request_field_set_type_hint(field, "screenname");
	gaim_request_field_set_required(field, TRUE);
	gaim_request_field_group_add_field(group, field);

	field = gaim_request_field_account_new("account", _("_Account"), NULL);
	gaim_request_field_set_visible(field,
		(gaim_connections_get_all() != NULL &&
		 gaim_connections_get_all()->next != NULL));
	gaim_request_field_set_required(field, TRUE);
	gaim_request_field_group_add_field(group, field);

	gaim_request_fields(gaim_get_blist(), _("Get User Info"),
						NULL,
						_("Please enter the screen name of the person whose "
						  "info you would like to view."),
						fields,
						_("OK"), G_CALLBACK(get_info_cb),
						_("Cancel"), NULL,
						NULL);
}

/*------------------------------------------------------*/
/* Color Selection Dialog                               */
/*------------------------------------------------------*/

GtkWidget *fgcseld = NULL;
GtkWidget *bgcseld = NULL;

void show_fgcolor_dialog(GaimConversation *c, GtkWidget *color)
{
	GaimGtkConversation *gtkconv;
	GdkColor fgcolor;

	gtkconv = GAIM_GTK_CONVERSATION(c);

	gdk_color_parse(gaim_prefs_get_string("/gaim/gtk/conversations/fgcolor"),
					&fgcolor);

	if (fgcseld)
		return;
	
	fgcseld = gtk_color_selection_dialog_new(_("Select Text Color"));
	gtk_color_selection_set_current_color(GTK_COLOR_SELECTION
					      (GTK_COLOR_SELECTION_DIALOG(fgcseld)->colorsel), &fgcolor);
	g_signal_connect(G_OBJECT(fgcseld), "delete_event",
			 G_CALLBACK(destroy_colorsel), (void *)1);
	g_signal_connect(G_OBJECT(GTK_COLOR_SELECTION_DIALOG(fgcseld)->cancel_button),
			 "clicked", G_CALLBACK(destroy_colorsel), (void *)1);
	g_signal_connect(G_OBJECT(GTK_COLOR_SELECTION_DIALOG(fgcseld)->ok_button), "clicked",
			 G_CALLBACK(apply_color_dlg), (void *)1);
	gtk_widget_realize(fgcseld);
	gtk_widget_show(fgcseld);
	gdk_window_raise(fgcseld->window);
	return;
}

void show_bgcolor_dialog(GaimConversation *c, GtkWidget *color)
{
	GaimGtkConversation *gtkconv;
	GdkColor bgcolor;

	gtkconv = GAIM_GTK_CONVERSATION(c);

	gdk_color_parse(gaim_prefs_get_string("/gaim/gtk/conversations/bgcolor"),
					&bgcolor);

	if (bgcseld)
		return;
	
	bgcseld = gtk_color_selection_dialog_new(_("Select Background Color"));
	gtk_color_selection_set_current_color(GTK_COLOR_SELECTION
					      (GTK_COLOR_SELECTION_DIALOG(bgcseld)->colorsel), &bgcolor);
	g_signal_connect(G_OBJECT(bgcseld), "delete_event",
			 G_CALLBACK(destroy_colorsel), NULL);
	g_signal_connect(G_OBJECT(GTK_COLOR_SELECTION_DIALOG(bgcseld)->cancel_button),
			 "clicked", G_CALLBACK(destroy_colorsel), NULL);
	g_signal_connect(G_OBJECT(GTK_COLOR_SELECTION_DIALOG(bgcseld)->ok_button), "clicked",
			 G_CALLBACK(apply_color_dlg), (void *)2);
	gtk_widget_realize(bgcseld);
	gtk_widget_show(bgcseld);
	gdk_window_raise(bgcseld->window);
	return;
}


/*------------------------  ----------------------------------------------*/
/*  Font Selection Dialog                                                 */
/*------------------------------------------------------------------------*/


void destroy_fontsel(GtkWidget *w, gpointer d)
{
	gtk_widget_destroy(fontseld);
	fontseld = NULL;
}

void show_font_dialog(GaimConversation *c, GtkWidget *font)
{
	GaimGtkConversation *gtkconv;
	char fonttif[128];
	const char *fontface;

	gtkconv = GAIM_GTK_CONVERSATION(c);


	if (fontseld)
		return;
	
	fontseld = gtk_font_selection_dialog_new(_("Select Font"));
	
	fontface = gaim_prefs_get_string("/gaim/gtk/conversations/font_face");
	
	if (fontface != NULL && *fontface != '\0') {
		g_snprintf(fonttif, sizeof(fonttif), "%s 12", fontface);
		gtk_font_selection_dialog_set_font_name(GTK_FONT_SELECTION_DIALOG(fontseld),
							fonttif);
	} else {
		gtk_font_selection_dialog_set_font_name(GTK_FONT_SELECTION_DIALOG(fontseld),
							DEFAULT_FONT_FACE " 12");
	}
	
	g_signal_connect(G_OBJECT(fontseld), "delete_event",
			 G_CALLBACK(destroy_fontsel), NULL);
	g_signal_connect(G_OBJECT(GTK_FONT_SELECTION_DIALOG(fontseld)->cancel_button),
			 "clicked", G_CALLBACK(destroy_fontsel), NULL);
	g_signal_connect(G_OBJECT(GTK_FONT_SELECTION_DIALOG(fontseld)->ok_button), "clicked",
			 G_CALLBACK(apply_font_dlg), fontseld);
	gtk_widget_realize(fontseld);
	gtk_widget_show(fontseld);
	gdk_window_raise(fontseld->window);
	return;

}

/*------------------------------------------------------------------------*/
/*  The dialog for new away messages                                      */
/*------------------------------------------------------------------------*/

static void away_mess_destroy(GtkWidget *widget, struct create_away *ca)
{
	destroy_dialog(NULL, ca->window);
	g_free(ca);
}

static void away_mess_destroy_ca(GtkWidget *widget, GdkEvent *event, struct create_away *ca)
{
	away_mess_destroy(NULL, ca);
}

static struct away_message *save_away_message(struct create_away *ca)
{
	struct away_message *am;
	gchar *away_message;

	if (!ca->mess)
		am = g_new0(struct away_message, 1);
	else {
		am = ca->mess;
	}

	g_snprintf(am->name, sizeof(am->name), "%s", gtk_entry_get_text(GTK_ENTRY(ca->entry)));
	away_message = gtk_imhtml_get_markup(GTK_IMHTML(ca->text));

	g_snprintf(am->message, sizeof(am->message), "%s", away_message);
	g_free(away_message);

	if (!ca->mess)
		away_messages = g_slist_insert_sorted(away_messages, am, sort_awaymsg_list);

	do_away_menu(NULL);
	gaim_status_sync();

	return am;
}

int check_away_mess(struct create_away *ca, int type)
{
	gchar *msg;
	if ((strlen(gtk_entry_get_text(GTK_ENTRY(ca->entry))) == 0) && (type == 1)) {
		/* We shouldn't allow a blank title */
		gaim_notify_error(NULL, NULL,
						  _("You cannot save an away message with a "
							"blank title"),
						  _("Please give the message a title, or choose "
							"\"Use\" to use without saving."));
		return 0;
	}

	msg = gtk_imhtml_get_text(GTK_IMHTML(ca->text), NULL, NULL);

	if ((type <= 1) && ((msg == NULL) || (*msg == '\0'))) {
		/* We shouldn't allow a blank message */
		gaim_notify_error(NULL, NULL,
						  _("You cannot create an empty away message"), NULL);
		return 0;
	}

	g_free(msg);

	return 1;
}

void save_away_mess(GtkWidget *widget, struct create_away *ca)
{
	if (!check_away_mess(ca, 1))
		return;

	save_away_message(ca);

	away_mess_destroy(NULL, ca);
}

void use_away_mess(GtkWidget *widget, struct create_away *ca)
{
	static struct away_message am;
	gchar *away_message;

	if (!check_away_mess(ca, 0))
		return;

	g_snprintf(am.name, sizeof(am.name), "%s", gtk_entry_get_text(GTK_ENTRY(ca->entry)));
	away_message = gtk_imhtml_get_markup(GTK_IMHTML(ca->text));

	g_snprintf(am.message, sizeof(am.message), "%s", away_message);
	g_free(away_message);

	do_away_message(NULL, &am);

	away_mess_destroy(NULL, ca);
}

void su_away_mess(GtkWidget *widget, struct create_away *ca)
{
	if (!check_away_mess(ca, 1))
		return;

	do_away_message(NULL, save_away_message(ca));

	away_mess_destroy(NULL, ca);
}

void create_away_mess(GtkWidget *widget, void *dummy)
{
	GtkWidget *vbox, *hbox;
	GtkWidget *label;
	GtkWidget *sw;
	GtkWidget *button;

	struct create_away *ca = g_new0(struct create_away, 1);

	/* Set up window */
	GAIM_DIALOG(ca->window);
	gtk_widget_set_size_request(ca->window, -1, 250);
	gtk_container_set_border_width(GTK_CONTAINER(ca->window), 6);
	gtk_window_set_role(GTK_WINDOW(ca->window), "away_mess");
	gtk_window_set_title(GTK_WINDOW(ca->window), _("New away message"));
	g_signal_connect(G_OBJECT(ca->window), "delete_event",
			   G_CALLBACK(away_mess_destroy_ca), ca);

	/*
	 * This would be higgy... but I think it's pretty ugly  --Mark
	 * If you want to use this, make sure you add the vbox to the hbox below
	 */
	/*
	hbox = gtk_hbox_new(FALSE, 12);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 12);
	gtk_container_add(GTK_CONTAINER(ca->window), hbox);
	*/

	vbox = gtk_vbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(ca->window), vbox);

	/* Away message title */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new(_("Away title: "));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	ca->entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), ca->entry, TRUE, TRUE, 0);
	gaim_set_accessible_label (ca->entry, label);

	/* Toolbar */
	ca->toolbar = gtk_imhtmltoolbar_new();
	gtk_box_pack_start(GTK_BOX(vbox), ca->toolbar, FALSE, FALSE, 0);

	/* Away message text */
	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
								   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

	ca->text = gtk_imhtml_new(NULL, NULL);
	gtk_imhtml_set_editable(GTK_IMHTML(ca->text), TRUE);
	gtk_imhtml_set_format_functions(GTK_IMHTML(ca->text), GTK_IMHTML_ALL ^ GTK_IMHTML_IMAGE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ca->text), GTK_WRAP_WORD_CHAR);

	gtk_imhtml_smiley_shortcuts(GTK_IMHTML(ca->text),
			gaim_prefs_get_bool("/gaim/gtk/conversations/smiley_shortcuts"));
	gtk_imhtml_html_shortcuts(GTK_IMHTML(ca->text),
			gaim_prefs_get_bool("/gaim/gtk/conversations/html_shortcuts"));
	if (gaim_prefs_get_bool("/gaim/gtk/conversations/spellcheck"))
		gaim_gtk_setup_gtkspell(GTK_TEXT_VIEW(ca->text));
	gtk_imhtmltoolbar_attach(GTK_IMHTMLTOOLBAR(ca->toolbar), ca->text);
	gtk_imhtmltoolbar_associate_smileys(GTK_IMHTMLTOOLBAR(ca->toolbar), "default");
	gaim_setup_imhtml(ca->text);

	gtk_container_add(GTK_CONTAINER(sw), ca->text);

	if (dummy) {
		struct away_message *amt;
		GtkTreeIter iter;
		GtkListStore *ls = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dummy)));
		GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(dummy));
		GValue val = { 0, };

		if (! gtk_tree_selection_get_selected (sel, (GtkTreeModel**)&ls, &iter))
			return;
		gtk_tree_model_get_value (GTK_TREE_MODEL(ls), &iter, 1, &val);
		amt = g_value_get_pointer (&val);
		gtk_entry_set_text(GTK_ENTRY(ca->entry), amt->name);
		gtk_imhtml_append_text_with_images(GTK_IMHTML(ca->text), amt->message, 0, NULL);
		ca->mess = amt;
	}

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	button = gaim_pixbuf_button_from_stock(_("Save"), GTK_STOCK_SAVE, GAIM_BUTTON_HORIZONTAL);
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(save_away_mess), ca);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

	button = gaim_pixbuf_button_from_stock(_("Save & Use"), GTK_STOCK_OK, GAIM_BUTTON_HORIZONTAL);
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(su_away_mess), ca);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

	button = gaim_pixbuf_button_from_stock(_("Use"), GTK_STOCK_EXECUTE, GAIM_BUTTON_HORIZONTAL);
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(use_away_mess), ca);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

	button = gaim_pixbuf_button_from_stock(_("Cancel"), GTK_STOCK_CANCEL, GAIM_BUTTON_HORIZONTAL);
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(away_mess_destroy), ca);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

	gtk_widget_show_all(ca->window);
	gtk_widget_grab_focus(ca->text);
}

static void
alias_chat_cb(GaimChat *chat, const char *new_alias)
{
	gaim_blist_alias_chat(chat, new_alias);
	gaim_blist_save();
}

void
alias_dialog_blist_chat(GaimChat *chat)
{
	gaim_request_input(NULL, _("Alias Chat"), NULL,
					   _("Enter an alias for this chat."),
					   chat->alias, FALSE, FALSE, NULL,
					   _("Alias"), G_CALLBACK(alias_chat_cb),
					   _("Cancel"), NULL, chat);
}

static void
alias_contact_cb(GaimContact *contact, const char *new_alias)
{
	gaim_contact_set_alias(contact, new_alias);
	gaim_blist_save();
}

void
alias_dialog_contact(GaimContact *contact)
{
	gaim_request_input(NULL, _("Alias Contact"), NULL,
					   _("Enter an alias for this contact."),
					   contact->alias, FALSE, FALSE, NULL,
					   _("Alias"), G_CALLBACK(alias_contact_cb),
					   _("Cancel"), NULL, contact);
}

static void
alias_buddy_cb(GaimBuddy *buddy, const char *alias)
{
	gaim_blist_alias_buddy(buddy, (alias != NULL && *alias != '\0') ? alias : NULL);
	serv_alias_buddy(buddy);
	gaim_blist_save();
}

void
alias_dialog_bud(GaimBuddy *b)
{
	char *secondary = g_strdup_printf(_("Enter an alias for %s."), b->name);

	gaim_request_input(NULL, _("Alias Buddy"), NULL,
					   secondary, b->alias, FALSE, FALSE, NULL,
					   _("Alias"), G_CALLBACK(alias_buddy_cb),
					   _("Cancel"), NULL, b);

	g_free(secondary);
}
