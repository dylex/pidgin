#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "debug.h"
#include "gnt.h"
#include "gntbox.h"
#include "gntmenu.h"
#include "gntstyle.h"
#include "gntwm.h"
#include "gntwindow.h"
#include "gntlabel.h"

#include "blist.h"

#define TYPE_FULL				(full_get_gtype())

typedef struct _Full
{
	GntWM inherit;

	gboolean borders;
	int blistwidth;
} Full;

typedef struct _FullClass
{
	GntWMClass inherit;
} FullClass;

GType full_get_gtype(void);
void gntwm_init(GntWM **wm);

static void (*org_new_window)(GntWM *wm, GntWidget *win);

static void
draw_line_separators(Full *full)
{
	/*
	wclear(stdscr);
	if (full->blistwidth >= 0)
		mvwvline(stdscr, 0, getmaxx(stdscr) - full->blistwidth - 1,
				ACS_VLINE | COLOR_PAIR(GNT_COLOR_NORMAL), getmaxy(stdscr) - 1);
	*/
}

static gboolean
is_blist(GntWidget *win)
{
	const char *name = gnt_widget_get_name(win);
	if (name && strcmp(name, "buddylist") == 0)
		return TRUE;
	return FALSE;
}

static void
relocate_win(Full *full, GntWidget *win, int x, int y, int w, int h)
{
	GntNode *node = win->wmnode; // g_hash_table_lookup(GNT_WM(full)->nodes, win);
	// purple_debug(PURPLE_DEBUG_INFO, "full", "relocate %s %p %p %d,%d,%d,%d\n", gnt_widget_get_name(win), win, win->parent, x,y,w,h);
	if (!full->borders && !GNT_WIDGET_IS_FLAG_SET(win, GNT_WIDGET_NO_BORDER))
	{
		GNT_WIDGET_SET_FLAGS(win, GNT_WIDGET_NO_BORDER | GNT_WIDGET_NO_SHADOW);
		w += 2; h += 2;
	}
	if (node)
	{
		hide_panel(node->panel);
	}
	gnt_widget_set_size(win, w, h);
	gnt_widget_set_position(win, x, y);
	if (node)
	{
		wresize(node->window, h, w);
		replace_panel(node->panel, node->window);
		show_panel(node->panel);
		move_panel(node->panel, y, x);
	}
}

static void
place_win(Full *full, GntWidget *win)
{
	if (is_blist(win))
	{
		if (full->blistwidth <= 0)
			gnt_widget_get_size(win, &full->blistwidth, NULL);
		relocate_win(full, win, getmaxx(stdscr)-full->blistwidth-full->borders, 0, full->blistwidth+full->borders, getmaxy(stdscr)-1);
	}
	else if (!GNT_IS_MENU(win) && !GNT_WIDGET_IS_FLAG_SET(win, GNT_WIDGET_TRANSIENT)) {
		relocate_win(full, win, 0, 0, getmaxx(stdscr) - full->blistwidth, getmaxy(stdscr)-1);
	}
}

static void
full_new_window(GntWM *wm, GntWidget *win)
{
	Full *full = (Full *)wm;
	if (is_blist(win))
		GNT_WIDGET_SET_FLAGS(win, GNT_WIDGET_SWITCH_SKIP);
	place_win(full, win);
	org_new_window(wm, win);
}

static void
full_terminal_refresh(GntWM *wm)
{
	Full *full = (Full *)wm;
	GList *iter;
	for (iter = wm->cws->list; iter; iter = iter->next) {
		GntWidget *win = GNT_WIDGET(iter->data);
		place_win(full, win);
	}

	draw_line_separators(full);
	// update_panels();
	// doupdate();
}

static gboolean
focus_blist(GntBindable *bindable, GList *null)
{
	purple_blist_show();
	return TRUE;
}

static gboolean
return_false(GntWM *wm, GntWidget *w, int *a, int *b)
{
	// purple_debug(PURPLE_DEBUG_INFO, "full", "confirm %s %p %p %d,%d\n", gnt_widget_get_name(w), w, w->parent, *a,*b);
	return FALSE;
}

static void
full_class_init(FullClass *klass)
{
	GntWMClass *pclass = GNT_WM_CLASS(klass);

	org_new_window = pclass->new_window;

	pclass->new_window = full_new_window;
	//pclass->window_resized = full_window_resized;
	//pclass->close_window = full_close_window;
	pclass->window_resize_confirm = return_false;
	pclass->window_move_confirm = return_false;
	pclass->terminal_refresh = full_terminal_refresh;

	gnt_bindable_class_register_action(GNT_BINDABLE_CLASS(klass), "window-blist", focus_blist, "\033" "b", NULL);

	gnt_style_read_actions(G_OBJECT_CLASS_TYPE(klass), GNT_BINDABLE_CLASS(klass));
	GNTDEBUG;
}

void gntwm_init(GntWM **wm)
{
	char *style = NULL;
	Full *full;

	full = g_object_new(TYPE_FULL, NULL);
	*wm = GNT_WM(full);

	style = gnt_style_get_from_name("full", "blist-width");
	full->blistwidth = style ? atoi(style) : 0;
	g_free(style);

	style = gnt_style_get_from_name("full", "borders");
	full->borders = gnt_style_parse_bool(style);
	g_free(style);
}

GType full_get_gtype(void)
{
	static GType type = 0;

	if(type == 0) {
		static const GTypeInfo info = {
			sizeof(FullClass),
			NULL,           /* base_init		*/
			NULL,           /* base_finalize	*/
			(GClassInitFunc)full_class_init,
			NULL,
			NULL,           /* class_data		*/
			sizeof(Full),
			0,              /* n_preallocs		*/
			NULL,	        /* instance_init	*/
			NULL
		};

		type = g_type_register_static(GNT_TYPE_WM,
		                              "GntFull",
		                              &info, 0);
	}

	return type;
}

