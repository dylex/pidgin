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
 *
 */

#include "internal.h"
#include "gtkimhtml.h"
#include "gtkutils.h"
#include "stock.h"
#include "ui.h"

/* XXX For WEBSITE */
#include "gaim.h"

static GtkWidget *about = NULL;

static void destroy_about()
{
	if (about)
		gtk_widget_destroy(about);
	about = NULL;
}

void show_about(GtkWidget *w, void *data)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *logo;
	GtkWidget *label;
	GtkWidget *sw;
	GtkWidget *text;
	GtkWidget *bbox;
	GtkWidget *button;
	char *str, *labeltext;

	if (about != NULL)
		return;

	GAIM_DIALOG(about);
	gtk_window_set_default_size(GTK_WINDOW(about), 450, -1);
	gtk_window_set_title(GTK_WINDOW(about), _("About Gaim"));
	gtk_window_set_role(GTK_WINDOW(about), "about");
	gtk_window_set_resizable(GTK_WINDOW(about), TRUE);
	gtk_widget_realize(about);

	hbox = gtk_hbox_new(FALSE, 12);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 12);
	gtk_container_add(GTK_CONTAINER(about), hbox);

	vbox = gtk_vbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(hbox), vbox);

	logo = gtk_image_new_from_stock(GAIM_STOCK_LOGO, gtk_icon_size_from_name(GAIM_ICON_SIZE_LOGO));
	gtk_box_pack_start(GTK_BOX(vbox), logo, FALSE, FALSE, 0);

	labeltext = g_strdup_printf(_("<span weight=\"bold\" size=\"larger\">Gaim v%s</span>"), VERSION);
	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), labeltext);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	g_free(labeltext);

	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
	gtk_widget_set_size_request(sw, -1, 350);
	gtk_box_pack_start(GTK_BOX(vbox), sw, FALSE, FALSE, 0);

	text = gtk_imhtml_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(sw), text);
	gaim_setup_imhtml(text);

	gtk_imhtml_append_text(GTK_IMHTML(text),
			  _("Gaim is a modular Instant Messaging client capable of "
				  "using AIM, ICQ, Yahoo!, MSN, IRC, Jabber, Napster, "
				  "Zephyr, and Gadu-Gadu all at once.  It is written using "
				  "Gtk+ and is licensed under the GPL.<BR><BR>"), GTK_IMHTML_NO_SCROLL);

	gtk_imhtml_append_text(GTK_IMHTML(text),
			"<FONT SIZE=\"4\">URL:</FONT> <A HREF=\"" GAIM_WEBSITE "\">"
			GAIM_WEBSITE "</A><BR><BR>", GTK_IMHTML_NO_SCROLL);

	gtk_imhtml_append_text(GTK_IMHTML(text),
			_("<FONT SIZE=\"4\">IRC:</FONT> #gaim on irc.freenode.net"
			"<BR><BR>"), GTK_IMHTML_NO_SCROLL);

	/* Active Developers */
	str = g_strconcat(
		"<FONT SIZE=\"4\">", _("Active Developers"), ":</FONT><BR>"
		"  Rob Flynn (", _("maintainer"), ") "
		"&lt;<A HREF=\"mailto:gaim@robflynn.com\">gaim@robflynn.com</A>&gt;<BR>"
		"  Sean Egan (", _("lead developer"), ") "
		"&lt;<A HREF=\"mailto:sean.egan@binghamton.edu\">"
		"bj91704@binghamton.edu</A>&gt;<BR>"
		"  Christian 'ChipX86' Hammond (", _("developer & webmaster"), ")<BR>"
		"  Herman Bloggs (", _("win32 port"), ") "
		"&lt;<A HREF=\"mailto:hermanator12002@yahoo.com\">"
		"hermanator12002@yahoo.com</A>&gt;<BR>"
		"  Nathan 'faceprint' Walp (", _("developer"), ")<BR>"
		"  Mark 'KingAnt' Doliner (", _("developer"), ")<BR>"
		"  Ethan 'Paco-Paco' Blanton (", _("developer"), ")<br>"
		"  Luke 'LSchiere' Schierer (", _("support"), ")<BR>"
		"<BR>", NULL);
	gtk_imhtml_append_text(GTK_IMHTML(text), str, GTK_IMHTML_NO_SCROLL);
	g_free(str);

	/* Crazy Patch Writers */
	str = g_strconcat(
		"<FONT SIZE=\"4\">", _("Crazy Patch Writers"), ":</FONT><BR>"
		"  Benjamin Miller<BR>"
		"  Decklin Foster<BR>"
		"  Etan 'deryni' Reisner<BR>"
		"  Robert 'Robot101' McQueen<BR>"
		"  Tim 'marv' Ringenbach<br>"
		"  Kevin 'SimGuy' Stange<br>"
		"  Stu 'nosnilmot' Tomlinson<br>"
		"  Gary 'xgrimx' Kramlich<br>"
		"  Ka-Hing 'javabsp' Cheung<br>"
		"<BR>", NULL);
	gtk_imhtml_append_text(GTK_IMHTML(text), str, GTK_IMHTML_NO_SCROLL);
	g_free(str);

	/* Retired Developers */
	str = g_strconcat(
			"<FONT SIZE=\"4\">", _("Retired Developers"), ":</FONT><BR>"
			"  Adam Fritzler (", _("former libfaim maintainer"), ")<BR>"
			"  Eric Warmenhoven (", _("former lead developer"), ") "
			"&lt;<A HREF=\"mailto:warmenhoven@yahoo.com\">"
			"warmenhoven@yahoo.com</A>&gt;<BR>"
			"  Jim Duchek (", _("former maintainer"), ")<BR>"
			"  Jim Seymour (", _("former Jabber developer"), ")<BR>"
			"  Mark Spencer (", _("original author"), ") "
			"&lt;<A HREF=\"mailto:markster@marko.net\">"
			"markster@marko.net</A>&gt;<BR>"
			"  Syd Logan (", _("hacker and designated driver [lazy bum]"), 
			")<BR>"
			"<BR>", NULL);
	gtk_imhtml_append_text(GTK_IMHTML(text), str, GTK_IMHTML_NO_SCROLL);
	g_free(str);

	/* Current Translators */
	str = g_strconcat(
			"<FONT SIZE=\"4\">", _("Current Translators"), ":</FONT><BR>"
			"  <b>", _("Catalan"), " (ca)</b> - Robert Millan &lt;<a href=\"mailto: zeratul2@wanadoo.es\">zeratul2@wanadoo.es</a>&gt;<br>"
			"  <b>", _("Czech"), " (cs)</b> - Miloslav Trmac &lt;<a href=\"mailto: mitr@volny.cz\">mitr@volny.cz</a>&gt;<br>"
			"  <b>", _("Danish"), " (da)</b> - Morten Brix Pedersen &lt;<a href=\"mailto: morten@wtf.dk\">morten@wtf.dk</a>&gt;<br>"
			"  <b>", _("British English"), "(en_GB)</b> - Luke Ross &lt;<a href=\"mailto: lukeross@sys3175.co.uk\">lukeross@sys3175.co.uk</a>&gt;<br>"
			"  <b>", _("German"), " (de)</b> - Björn Voigt &lt;<a href=\"mailto: bjoern@cs.tu-berlin.de\">bjoern@cs.tu-berlin.de</a>&gt;<br>"
			"  <b>", _("Spanish"), " (es)</b> - Javier Fernández-Sanguino Peña &lt;<a href=\"mailto: jfs@debian.org\">jfs@debian.org</a>&gt;<br>"
			"  <b>", _("Finnish"), " (fi)</b> - Arto Alakulju &lt;<a href=\"mailto: arto@alakulju.net\">arto@alakulju.net</a>&gt;<br>"
			"  <b>", _("French"), " (fr)</b> - Éric Boumaour &lt;<a href=\"mailto: zongo_fr@users.sourceforge.net\">zongo_fr@users.sourceforge.net</a>&gt;<br>"
			"  <b>", _("Hebrew"), " (he)</b> - Pavel Bibergal &lt;<a href=\"mailto:cyberkm203@hotmail.com\">cyberkm203@hotmail.com</a>&gt;<br>"
			"  <b>", _("Hindi"), " (hi)</b> - Ravishankar Shrivastava &lt;<a href=\"mailto: raviratlami@yahoo.com\">raviratlami@yahoo.com</a>&gt;<br>"
			"  <b>", _("Hungarian"), " (hu)</b> - Zoltan Sutto &lt;<a href=\"mailto: suttozoltan@chello.hu\">suttozoltan@chello.hu</a>&gt;<br>"
			"  <b>", _("Italian"), " (it)</b> - Claudio Satriano &lt;<a href=\"mailto: satriano@na.infn.it\">satriano@na.infn.it</a>&gt;<br>"
			"  <b>", _("Korean"), " (ko)</b> - Kyung-uk Son &lt;<a href=\"mailto: vvs740@chol.com\">vvs740@chol.com</a>&gt;<br>"
			"  <b>", _("Dutch; Flemish"), " (nl)</b> - Vincent van Adrighem &lt;<a href=\"mailto: V.vanAdrighem@dirck.mine.nu\">V.vanAdrighem@dirck.mine.nu</a>&gt;<br>"
			"  <b>", _("Macedonian"), "(mk)</b> - Tomislav Markovski &lt;<a href=\"mailto: herrera@users.sf.net\">herrera@users.sf.net</a>&gt;<br>"
			"  <b>", _("Norwegian"), " (no)</b> - Petter Johan Olsen &lt;<a href=\"mailto:petter.olsen@cc.uit.no\">petter.olsen@cc.uit.no</a>&gt;<br>"
			"  <b>", _("Polish"), " (pl)</b> - Krzysztof &lt;<a href=\"mailto:krzysztof@foltman.com\">krzysztof@foltman.com</a>&gt;, Emil &lt;<a href=\"mailto:emil5@go2.pl\">emil5@go2.pl</a>&gt;<br>"
			"  <b>", _("Portuguese"), " (pt)</b> - Duarte Henriques &lt;<a href=\"mailto:duarte_henriques@myrealbox.com\">duarte_henriques@myrealbox.com</a>&gt;<br>"
			"  <b>", _("Portuguese-Brazil"), " (pt_BR)</b> - Maurício de Lemos Rodrigues Collares Neto &lt;<a href=\"mailto: mauricioc@myrealbox.com\">mauricioc@myrealbox.com</a>&gt;<br>"
			"  <b>", _("Romanian"), " (ro)</b> - Mişu Moldovan &lt;<a href=\"mailto: dumol@go.ro\">dumol@go.ro</a>&gt;<br>"
			"  <b>", _("Russian"), "(ru)</b> - Alexandre Prokoudine &lt;<a href=\"mailto: avp@altlinux.ru\">avp@altlinux.ru</a>&gt;<br>"
			"  <b>", _("Serbian"), " (sr)</b> - Danilo Šegan &lt;<a href=\"mailto: dsegan@gmx.net\">dsegan@gmx.net</a>&gt;, Aleksandar Urosevic &lt;<a href=\"mailto: urke@users.sourceforge.net\">urke@users.sourceforge.net</a>&gt;<br>"
			"  <b>", _("Slovenian"), " (sl)</b> - Matjaz Horvat &lt;&gt;<br>"
			"  <b>", _("Swedish"), " (sv)</b> - Tore Lundqvist &lt;<a href=\"mailto: tlt@mima.x.se\">tlt@mima.x.se</a>&gt;<br>"
			"  <b>", _("Vietnamese"), "(vi)</b> - T.M.Thanh, ", _("Gnome Vi Team"), ". &lt;<a href=\"mailto: gnomevi-list@lists.sf.net\">gnomevi-list@lists.sf.net</a>&gt;<br>"
			"  <b>", _("Simplified Chinese"), " (zh_CN)</b> - Funda Wang &lt;<a href=\"mailto: fundawang@linux.net.cn\">fundawang@linux.net.cn</a>&gt;<br>"
			"  <b>", _("Traditional Chinese"), " (zh_TW)</b> - Ambrose C. Li &lt;<a href=\"mailto: acli@ada.dhs.org\">acli@ada.dhs.org</a>&gt;, Paladin R. Liu &lt;<a href=\"mailto: paladin@ms1.hinet.net\">paladin@ms1.hinet.net</a>&gt;<br>"
			"<BR>", NULL);
	gtk_imhtml_append_text(GTK_IMHTML(text), str, GTK_IMHTML_NO_SCROLL);
	g_free(str);

	/* Past Translators */
	str = g_strconcat(
			"<FONT SIZE=\"4\">", _("Past Translators"), ":</FONT><BR>"
			"  <b>", _("Amharic"), " (am)</b> - Daniel Yacob<br>"
			"  <b>", _("Bulgarian"), " (bg)</b> - Hristo Todorov<br>"
			"  <b>", _("Catalan"), " (ca)</b> - JM Pérez Cáncer<br>"
			"  <b>", _("Czech"), " (cs)</b> - Honza Král<br>"
			"  <b>", _("German"), " (de)</b> - Daniel Seifert, Karsten Weiss<br>"
			"  <b>", _("Spanish"), " (es)</b> - Amaya Rodrigo, Alejandro G Villar, Nicolás Lichtmaier, JM Pérez Cáncer<br>"
			"  <b>", _("Finnish"), " (fi)</b> - Tero Kuusela<br>"
			"  <b>", _("French"), " (fr)</b> - Sébastien François, Stéphane Pontier, Stéphane Wirtel, Loïc Jeannin<br>"
			"  <b>", _("Italian"), " (it)</b> - Salvatore di Maggio<br>"
			"  <b>", _("Japanese"), " (ja)</b> - Ryosuke Kutsuna, Taku Yasui, Junichi Uekawa<br>"
			"  <b>", _("Korean"), " (ko)</b> - Sang-hyun S, A Ho-seok Lee<br>"
			"  <b>", _("Polish"), " (pl)</b> - Przemysław Sułek<br>"
			"  <b>", _("Russian"), " (ru)</b> - Sergey Volozhanin<br>"
			"  <b>", _("Slovak"), " (sk)</b> - Daniel Režný<br>"
			"  <b>", _("Swedish"), " (sv)</b> - Christian Rose<br>"
			"  <b>", _("Chinese"), " (zh_CN, zh_TW)</b> - Hashao, Rocky S. Lee<br>"
			"<BR>", NULL);
	gtk_imhtml_append_text(GTK_IMHTML(text), str, GTK_IMHTML_NO_SCROLL);
	g_free(str);

	gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw)), 0);

	/* Close Button */
	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

	button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);

	g_signal_connect_swapped(G_OBJECT(button), "clicked",
							 G_CALLBACK(destroy_about), G_OBJECT(about));
	g_signal_connect(G_OBJECT(about), "destroy",
					 G_CALLBACK(destroy_about), G_OBJECT(about));

	/* this makes the sizes not work? */
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_widget_grab_default(button);

	/* Let's give'em something to talk about -- woah woah woah */
	gtk_widget_show_all(about);
	gtk_window_present(GTK_WINDOW(about));
}
