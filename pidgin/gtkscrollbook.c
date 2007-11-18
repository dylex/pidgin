/*
 * @file gtkscrollbook.c GTK+ Scrolling notebook widget 
 * @ingroup pidgin
 */

/* pidgin
 *
 * Pidgin is the legal property of its developers, whose names are too numerous
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include "gtkscrollbook.h"


static void pidgin_scroll_book_init (PidginScrollBook *scroll_book);
static void pidgin_scroll_book_class_init (PidginScrollBookClass *klass);
static void pidgin_scroll_book_forall (GtkContainer *c, 
					 gboolean include_internals,
			 		 GtkCallback callback,
					 gpointer user_data);

GType
pidgin_scroll_book_get_type (void)
{
	static GType scroll_book_type = 0;

	if (!scroll_book_type)
	{
		static const GTypeInfo scroll_book_info =
		{
			sizeof (PidginScrollBookClass),
			NULL, /* base_init */
			NULL, /* base_finalize */
			(GClassInitFunc) pidgin_scroll_book_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (PidginScrollBook),
			0,
			(GInstanceInitFunc) pidgin_scroll_book_init,
			NULL  /* value_table */
		};

		scroll_book_type = g_type_register_static(GTK_TYPE_VBOX,
							 "PidginScrollBook",
							 &scroll_book_info,
							 0);
	}

	return scroll_book_type;
}

static void
scroll_left_cb(PidginScrollBook *scroll_book)
{
	int index;
	index = gtk_notebook_get_current_page(GTK_NOTEBOOK(scroll_book->notebook));

	if (index > 0)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(scroll_book->notebook), index - 1);
}

static void
scroll_right_cb(PidginScrollBook *scroll_book)
{
	int index, count;
	index = gtk_notebook_get_current_page(GTK_NOTEBOOK(scroll_book->notebook));
#if GTK_CHECK_VERSION(2,2,0)
	count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(scroll_book->notebook));
#else
	count = g_list_length(GTK_NOTEBOOK(scroll_book->notebook)->children);
#endif	
	
	if (index + 1 < count)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(scroll_book->notebook), index + 1);
}

static void
refresh_scroll_box(PidginScrollBook *scroll_book, int index, int count)
{
	char *label;
	gtk_widget_show_all(GTK_WIDGET(scroll_book));
	if (count <= 1)
		gtk_widget_hide(GTK_WIDGET(scroll_book->hbox));
	else
		gtk_widget_show_all(GTK_WIDGET(scroll_book->hbox));
		
	
	label = g_strdup_printf("<span size='smaller' weight='bold'>(%d/%d)</span>", index+1, count);
	gtk_label_set_markup(GTK_LABEL(scroll_book->label), label);
	g_free(label);				      

	if (index == 0)
		gtk_widget_set_sensitive(scroll_book->left_arrow, FALSE);
	else
		gtk_widget_set_sensitive(scroll_book->left_arrow, TRUE);

	
	if (index +1== count)
		gtk_widget_set_sensitive(scroll_book->right_arrow, FALSE);
	else
		gtk_widget_set_sensitive(scroll_book->right_arrow, TRUE);
}


static void
page_count_change_cb(PidginScrollBook *scroll_book)
{
	int count;
	int index = gtk_notebook_get_current_page(GTK_NOTEBOOK(scroll_book->notebook));
#if GTK_CHECK_VERSION(2,2,0)
	count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(scroll_book->notebook));
#else
	count = g_list_length(GTK_NOTEBOOK(scroll_book->notebook)->children);
#endif
	refresh_scroll_box(scroll_book, index, count);
	
}

static void
switch_page_cb(GtkNotebook *notebook, GtkNotebookPage *page, guint page_num, PidginScrollBook *scroll_book)
{
	int count;
#if GTK_CHECK_VERSION(2,2,0)
	count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(scroll_book->notebook));
#else
	count = g_list_length(GTK_NOTEBOOK(scroll_book->notebook)->children);
#endif
	refresh_scroll_box(scroll_book, page_num, count);
}

static void
pidgin_scroll_book_add(GtkContainer *container, GtkWidget *widget)
{
	GList *children;
	PidginScrollBook *scroll_book;

	g_return_if_fail(GTK_IS_WIDGET (widget));
	g_return_if_fail (widget->parent == NULL);

	scroll_book = PIDGIN_SCROLL_BOOK(container);
	children = scroll_book->children;
	children = g_list_append(children, widget);
	gtk_widget_show(widget);
	gtk_notebook_append_page(GTK_NOTEBOOK(PIDGIN_SCROLL_BOOK(container)->notebook), widget, NULL);
	page_count_change_cb(PIDGIN_SCROLL_BOOK(container));
}

static void
pidgin_scroll_book_remove(GtkContainer *container, GtkWidget *widget)
{
	int page;
	GList *children;
	GtkWidget *child;
	PidginScrollBook *scroll_book;
	g_return_if_fail(GTK_IS_WIDGET(widget));

	scroll_book = PIDGIN_SCROLL_BOOK(container);
	children = scroll_book->children;

	while (children) {
		child = children->data;
		if (child == widget) {
			gtk_widget_unparent (widget);
			children = g_list_remove_link (scroll_book->children, children);
			g_list_free(children);
			break;
		}
	}

	page = gtk_notebook_page_num(GTK_NOTEBOOK(PIDGIN_SCROLL_BOOK(container)->notebook), widget);
	if (page >= 0) {
		gtk_notebook_remove_page(GTK_NOTEBOOK(PIDGIN_SCROLL_BOOK(container)->notebook), page);
	}
}

static void
pidgin_scroll_book_forall(GtkContainer *container,
			   gboolean include_internals,
			   GtkCallback callback,
			   gpointer callback_data)
{
	GList *children;
	PidginScrollBook *scroll_book;

	g_return_if_fail(GTK_IS_CONTAINER(container));

	scroll_book = PIDGIN_SCROLL_BOOK(container);

	if (include_internals) {
		(*callback)(scroll_book->hbox, callback_data);
		(*callback)(scroll_book->notebook, callback_data);
	}

	children = scroll_book->children;

	while (children) {
		GtkWidget *child;
		child = children->data;
		children = children->next;
		(*callback)(child, callback_data);
	}
}

static void
pidgin_scroll_book_class_init (PidginScrollBookClass *klass)
{
	GtkContainerClass *container_class = (GtkContainerClass*)klass;

	container_class->add = pidgin_scroll_book_add;
	container_class->remove = pidgin_scroll_book_remove;
	container_class->forall = pidgin_scroll_book_forall;	
	
}

static void
pidgin_scroll_book_init (PidginScrollBook *scroll_book)
{
	GtkWidget *eb;

	scroll_book->hbox = gtk_hbox_new(FALSE, 0);

	eb = gtk_event_box_new();
	gtk_box_pack_end(GTK_BOX(scroll_book->hbox), eb, FALSE, FALSE, 0);
	scroll_book->right_arrow = gtk_arrow_new(GTK_ARROW_RIGHT, GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(eb), scroll_book->right_arrow);
	g_signal_connect_swapped(G_OBJECT(eb), "button-press-event", G_CALLBACK(scroll_right_cb), scroll_book);

	scroll_book->label = gtk_label_new(NULL);
	gtk_box_pack_end(GTK_BOX(scroll_book->hbox), scroll_book->label, FALSE, FALSE, 0);

	eb = gtk_event_box_new();
	gtk_box_pack_end(GTK_BOX(scroll_book->hbox), eb, FALSE, FALSE, 0);
	scroll_book->left_arrow = gtk_arrow_new(GTK_ARROW_LEFT, GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(eb), scroll_book->left_arrow);
	g_signal_connect_swapped(G_OBJECT(eb), "button-press-event", G_CALLBACK(scroll_left_cb), scroll_book);

	gtk_box_pack_start(GTK_BOX(scroll_book), scroll_book->hbox, FALSE, FALSE, 0);
	
	scroll_book->notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(scroll_book->notebook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(scroll_book->notebook), FALSE);
	
	gtk_box_pack_start(GTK_BOX(scroll_book), scroll_book->notebook, TRUE, TRUE, 0);
	
	g_signal_connect_swapped(G_OBJECT(scroll_book->notebook), "remove", G_CALLBACK(page_count_change_cb), scroll_book);
	g_signal_connect(G_OBJECT(scroll_book->notebook), "switch-page", G_CALLBACK(switch_page_cb), scroll_book);
	gtk_widget_show_all(scroll_book->notebook);
}



GtkWidget *
pidgin_scroll_book_new()
{
	return g_object_new(PIDGIN_TYPE_SCROLL_BOOK, NULL);
}
