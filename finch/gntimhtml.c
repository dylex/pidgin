/*
* @file gntimhtml.c GNT IMHtml
* @ingroup finch
*/

/* finch
*
* Finch is the legal property of its developers, whose names are too numerous
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

#include "gnt.h"
#include "gnttextview.h"
#include "gntimhtml.h"

#include "util.h"
#include <string.h>
#include <ctype.h>

/******************************************************************************
* GntIMHtml API
*****************************************************************************/

/* This could be a separate class, but at present there's no need.
* Most of the following was copied from pidgin/gtkimhtml */

#define VALID_TAG(x)	if (!g_ascii_strncasecmp (string, x ">", strlen (x ">"))) {	\
			*tag = g_strndup (string, strlen (x));		\
			*len = strlen (x) + 1;				\
			return TRUE;					\
		}							\
		(*type)++

#define VALID_OPT_TAG(x) if (!g_ascii_strncasecmp (string, x " ", strlen (x " "))) {	\
				const gchar *c = string + strlen (x " ");	\
				gchar e = '"';					\
				gboolean quote = FALSE;				\
				while (*c) {					\
					if (*c == '"' || *c == '\'') {		\
						if (quote && (*c == e))		\
							quote = !quote;		\
						else if (!quote) {		\
							quote = !quote;		\
							e = *c;			\
						}				\
					} else if (!quote && (*c == '>'))	\
						break;				\
					c++;					\
				}						\
				if (*c) {					\
					*tag = g_strndup (string, c - string);	\
					*len = c - string + 1;			\
					return TRUE;				\
				}						\
			}							\
			(*type)++

static gboolean
gnt_imhtml_is_tag (const gchar *string,
	   gchar      **tag,
	   gint        *len,
	   gint        *type)
{
	char *close;
	*type = 1;

	if (!(close = strchr (string, '>')))
		return FALSE;

	VALID_TAG ("B");
	VALID_TAG ("BOLD");
	VALID_TAG ("/B");
	VALID_TAG ("/BOLD");
	VALID_TAG ("I");
	VALID_TAG ("ITALIC");
	VALID_TAG ("/I");
	VALID_TAG ("/ITALIC");
	VALID_TAG ("U");
	VALID_TAG ("UNDERLINE");
	VALID_TAG ("/U");
	VALID_TAG ("/UNDERLINE");
	VALID_TAG ("S");
	VALID_TAG ("STRIKE");
	VALID_TAG ("/S");
	VALID_TAG ("/STRIKE");
	VALID_TAG ("SUB");
	VALID_TAG ("/SUB");
	VALID_TAG ("SUP");
	VALID_TAG ("/SUP");
	VALID_TAG ("PRE");
	VALID_TAG ("/PRE");
	VALID_TAG ("TITLE");
	VALID_TAG ("/TITLE");
	VALID_TAG ("BR");
	VALID_TAG ("HR");
	VALID_TAG ("/FONT");
	VALID_TAG ("/A");
	VALID_TAG ("P");
	VALID_TAG ("/P");
	VALID_TAG ("H3");
	VALID_TAG ("/H3");
	VALID_TAG ("HTML");
	VALID_TAG ("/HTML");
	VALID_TAG ("BODY");
	VALID_TAG ("/BODY");
	VALID_TAG ("FONT");
	VALID_TAG ("HEAD");
	VALID_TAG ("/HEAD");
	VALID_TAG ("BINARY");
	VALID_TAG ("/BINARY");

	VALID_OPT_TAG ("HR");
	VALID_OPT_TAG ("FONT");
	VALID_OPT_TAG ("BODY");
	VALID_OPT_TAG ("A");
	VALID_OPT_TAG ("IMG");
	VALID_OPT_TAG ("P");
	VALID_OPT_TAG ("H3");
	VALID_OPT_TAG ("HTML");

	VALID_TAG ("CITE");
	VALID_TAG ("/CITE");
	VALID_TAG ("EM");
	VALID_TAG ("/EM");
	VALID_TAG ("STRONG");
	VALID_TAG ("/STRONG");

	VALID_OPT_TAG ("SPAN");
	VALID_TAG ("/SPAN");
	VALID_TAG ("BR/"); /* hack until gtkimhtml handles things better */
	VALID_TAG ("IMG");
	VALID_TAG("SPAN");
	VALID_OPT_TAG("BR");

	if (!g_ascii_strncasecmp(string, "!--", strlen ("!--"))) {
		gchar *e = strstr (string + strlen("!--"), "-->");
		if (e) {
			*len = e - string + strlen ("-->");
			*tag = g_strndup (string + strlen ("!--"), *len - strlen ("!---->"));
			return TRUE;
		}
	}

	*type = -1;
	*len = close - string + 1;
	*tag = g_strndup(string, *len - 1);
	return TRUE;
}

static gchar*
gnt_imhtml_get_html_opt (gchar       *tag,
		 const gchar *opt)
{
	gchar *t = tag;
	gchar *e, *a;
	gchar *val;
	gint len;
	const gchar *c;
	GString *ret;

	while (g_ascii_strncasecmp (t, opt, strlen (opt))) {
		gboolean quote = FALSE;
		if (*t == '\0') break;
		while (*t && !((*t == ' ') && !quote)) {
			if (*t == '\"')
				quote = ! quote;
			t++;
		}
		while (*t && (*t == ' ')) t++;
	}

	if (!g_ascii_strncasecmp (t, opt, strlen (opt))) {
		t += strlen (opt);
	} else {
		return NULL;
	}

	if ((*t == '\"') || (*t == '\'')) {
		e = a = ++t;
		while (*e && (*e != *(t - 1))) e++;
		if  (*e == '\0') {
			return NULL;
		} else
			val = g_strndup(a, e - a);
	} else {
		e = a = t;
		while (*e && !isspace ((gint) *e)) e++;
		val = g_strndup(a, e - a);
	}

	ret = g_string_new("");
	e = val;
	while(*e) {
		if((c = purple_markup_unescape_entity(e, &len))) {
			ret = g_string_append(ret, c);
			e += len;
		} else {
			gunichar uni = g_utf8_get_char(e);
			ret = g_string_append_unichar(ret, uni);
			e = g_utf8_next_char(e);
		}
	}

	g_free(val);

	return g_string_free(ret, FALSE);
}

static int gnt_imhtml_fgcolor(const char *color)
{
	if (!g_ascii_strcasecmp(color, "red"))
		return gnt_color_pair(GNT_COLOR_RED);
	else if (!g_ascii_strcasecmp(color, "green"))
		return gnt_color_pair(GNT_COLOR_GREEN);
	else if (!g_ascii_strcasecmp(color, "yellow"))
		return gnt_color_pair(GNT_COLOR_YELLOW);
	else if (!g_ascii_strcasecmp(color, "blue"))
		return gnt_color_pair(GNT_COLOR_BLUE);
	else if (!g_ascii_strcasecmp(color, "magenta"))
		return gnt_color_pair(GNT_COLOR_MAGENTA);
	else if (!g_ascii_strcasecmp(color, "cyan"))
		return gnt_color_pair(GNT_COLOR_CYAN);
	else if (!g_ascii_strcasecmp(color, "white"))
		return gnt_color_pair(GNT_COLOR_WHITE);
	else if (!g_ascii_strcasecmp(color, "black"))
		return gnt_color_pair(GNT_COLOR_BLACK);
	return 0;
}

void gnt_imhtml_append_markup(GntTextView *view, const char *text, GntTextFormatFlags orig_flags)
{
	gchar *ws;
	gint wpos=0;
	const gchar *c;

	guint	bold = 0,
		underline = 0;
	gchar *href = NULL;
	gboolean br = FALSE;

	GntTextFormatFlags flags = orig_flags;
	GSList *fonts = NULL;

	g_return_if_fail (view != NULL);
	g_return_if_fail (GNT_IS_TEXT_VIEW (view));
	g_return_if_fail (text != NULL);
	c = text;
	ws = g_malloc(strlen(text) + 1);
	ws[0] = 0;

	#define ADD_TEXT if (wpos) { ws[wpos] = '\0'; gnt_text_view_append_text_with_flags(view, ws, flags); ws[wpos = 0] = '\0'; }

	while (*c)
	{
		gchar *tag;
		gint tlen, type;
		const gchar *amp;
		if (*c == '<' && gnt_imhtml_is_tag (c + 1, &tag, &tlen, &type)) {
				c++;
				ws[wpos] = '\0';
				br = FALSE;
				switch (type)
				{
					/* these random numbers are awful */
					case 1:		/* B */
					case 2:		/* BOLD */
					case 54:	/* STRONG */
					case 52:	/* EM */
						if (!bold++)
						{
							ADD_TEXT;
							flags |= GNT_TEXT_FLAG_BOLD;
						}
						break;
					case 3:		/* /B */
					case 4:		/* /BOLD */
					case 55:	/* /STRONG */
					case 53:	/* /EM */
						if (bold && !--bold)
						{
							ADD_TEXT;
							flags &= ~GNT_TEXT_FLAG_BOLD | orig_flags;
						}
						break;
					case 5:		/* I */
					case 6:		/* ITALIC */
					case 9:		/* U */
					case 10:	/* UNDERLINE */
						ADD_TEXT;
						underline++;
						flags ^= GNT_TEXT_FLAG_UNDERLINE;
						break;
					case 7:		/* /I */
					case 8:		/* /ITALIC */
					case 11:	/* /U */
					case 12:	/* /UNDERLINE */
						if (underline)
						{
							ADD_TEXT;
							underline--;
							flags ^= GNT_TEXT_FLAG_UNDERLINE;
						}
						break;
					case 17:	/* SUB */
						ws[wpos++] = '_';
						break;
					case 19:	/* SUP */
						ws[wpos++] = '^';
						break;
					case 25:	/* BR */
					case 58:	/* BR/ */
					case 61:	/* BR (opt) */
					case 26:        /* HR */
					case 42:        /* HR (opt) */
						ws[wpos++] = '\n';
						br = TRUE;
						break;
					case 37:	/* FONT */
						fonts = g_slist_prepend(fonts, GINT_TO_POINTER(flags));
						break;
					case 43:	/* FONT (opt) */
						fonts = g_slist_prepend(fonts, GINT_TO_POINTER(flags));
						{
							gchar *color;
							color = gnt_imhtml_get_html_opt (tag, "COLOR=");

							if (color)
							{
								ADD_TEXT;
								flags &= ~A_COLOR;
								flags |= gnt_imhtml_fgcolor(color);
								g_free(color);
							}
						}
						break;
					case 27:	/* /FONT */
						if (fonts)
						{
							ADD_TEXT;
							flags &= ~A_COLOR;
							flags |= GPOINTER_TO_INT(fonts->data) & A_COLOR;
							fonts = g_slist_remove (fonts, fonts->data);

						}
						break;
					case 45:	/* A (opt) */
						ADD_TEXT;
						if (href)
							g_free(href);
						href = gnt_imhtml_get_html_opt(tag, "HREF=");
						flags ^= GNT_TEXT_FLAG_UNDERLINE;
						break;
					case 28:        /* /A    */
						ADD_TEXT;
						flags ^= GNT_TEXT_FLAG_UNDERLINE;
						if (href)
						{
							// ws[wpos++] = ' ';
							ws[wpos++] = '[';
							strcpy(&ws[wpos], href);
							wpos += strlen(href);
							ws[wpos++] = ']';
							g_free(href);
							href = NULL;
						}
						break;
					case 47:	/* P (opt) */
					case 48:	/* H3 (opt) */
					case 49:	/* HTML (opt) */
					case 50:	/* CITE */
					case 51:	/* /CITE */
					case 56:	/* SPAN (opt) */
						fonts = g_slist_prepend(fonts, GINT_TO_POINTER(flags));
						{
							gchar *style, *color = NULL, *textdec = NULL, *weight = NULL;
							style = gnt_imhtml_get_html_opt (tag, "style=");

							if (style)
							{
								color = purple_markup_get_css_property (style, "color");
								textdec = purple_markup_get_css_property (style, "text-decoration");
								weight = purple_markup_get_css_property (style, "font-weight");
							}

							if (color)
							{
								ADD_TEXT;
								flags &= ~A_COLOR;
								flags |= gnt_imhtml_fgcolor(color);
								g_free(color);
							}
							if (textdec)
							{
								if (!g_ascii_strcasecmp(textdec, "underline"))
								{
									ADD_TEXT;
									flags ^= GNT_TEXT_FLAG_UNDERLINE;
								}
								g_free(textdec);
							}
							if (weight)
							{
								if (!g_ascii_strcasecmp(weight, "normal"))
								{
									flags &= ~GNT_TEXT_FLAG_BOLD;
									flags |= orig_flags & GNT_TEXT_FLAG_BOLD;
								}
								else if (!g_ascii_strcasecmp(weight, "bold") || !g_ascii_strcasecmp(weight, "bolder"))
									flags |= GNT_TEXT_FLAG_BOLD;
								else if (!g_ascii_strcasecmp(weight, "lighter"))
									flags &= ~GNT_TEXT_FLAG_BOLD;
								g_free(weight);
							}

							g_free(style);
						}
						break;
					case 57:	/* /SPAN */
						if (fonts)
						{
							ADD_TEXT;
							flags = GPOINTER_TO_INT(fonts->data);
							fonts = g_slist_remove(fonts, fonts->data);
						}
						break;
					case 62:	/* comment */
						ADD_TEXT;
						break;
					default:
						break;
				}
				c += tlen;
				g_free(tag); /* This was allocated back in VALID_TAG() */
			} else if (*c == '&' && (amp = purple_markup_unescape_entity(c, &tlen))) {
				br = FALSE;
				while(*amp) {
					ws [wpos++] = *amp++;
				}
				c += tlen;
			} else if (*c == '\n') {
				if (!br) {  /* Don't insert a space immediately after an HTML break */
					/* A newline is defined by HTML as whitespace, which means we have to replace it with a word boundary.
					 * word breaks vary depending on the language used, so the correct thing to do is to use Pango to determine
					 * what language this is, determine the proper word boundary to use, and insert that. I'm just going to insert
					 * a space instead.  What are the non-English speakers going to do?  Complain in a language I'll understand?
					 * Bu-wahaha! */
					ws[wpos++] = ' ';
				}
				c++;
			} else {
				br = FALSE;
				ws [wpos++] = *c++;
			}
		}
		ADD_TEXT;

		while (fonts)
			fonts = g_slist_remove(fonts, fonts->data);

		g_free(ws);
}

