/*
 * gaim
 *
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
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/wait.h>
#include <gtk/gtk.h>
#include <ctype.h>
#include <pixmaps/aimicon.xpm>
#include "gaim.h"

static GdkPixmap *icon_pm = NULL;
static GdkBitmap *icon_bm = NULL;

char *full_date() {
	char * date;
	time_t tme;

	time(&tme);
	date = ctime(&tme);
	date[strlen(date)-1] = '\0';
	return date;
}

gint badchar(char c)
{
	switch(c) {
	case ' ':
	case ',':
	case '(':
	case ')':
	case '\0':
	case '\n':
	case '<':
	case '>':
		return 1;
	default:
		return 0;
	}
}


gchar *sec_to_text(gint sec)
{
        int hrs, min;
        char minutes[64];
        char hours[64];
        char *sep;
        char *ret = g_malloc(256);
        
        hrs = sec / 3600;
        min = sec % 3600;

        min = min / 60;
        sec = min % 60;

        if (min) {
                if (min == 1)
                        g_snprintf(minutes, sizeof(minutes), "%d minute.", min);
                else
                        g_snprintf(minutes, sizeof(minutes), "%d minutes.", min);
                sep = ", ";
        } else {
                if (!hrs)
                        g_snprintf(minutes, sizeof(minutes), "%d minutes.", min);
                else {
                        minutes[0] = '.';
                        minutes[1] = '\0';
                }
                sep = "";
        }

        if (hrs) {
                if (hrs == 1)
                        g_snprintf(hours, sizeof(hours), "%d hour%s", hrs, sep);
                else
                        g_snprintf(hours, sizeof(hours), "%d hours%s", hrs, sep);
        } else
                hours[0] = '\0';

        
        g_snprintf(ret, 256, "%s%s", hours, minutes);

        return ret;
}

gint linkify_text(char *text)
{
        char *c, *t;
        char *cpy = g_malloc(strlen(text) * 2 + 1);
        char url_buf[512];
        int cnt=0;
        /* Assumes you have a buffer able to cary at least BUF_LEN * 2 bytes */

        strncpy(cpy, text, strlen(text));
        cpy[strlen(text)] = 0;
        c = cpy;
        while(*c) {
                if (!strncasecmp(c, "<A", 2)) {
                        while(1) {
                                if (!strncasecmp(c, "/A>", 3)) {
                                        break;
                                }
                                text[cnt++] = *c;
                                c++;
                                if (!(*c))
                                        break;
                        }
                } else if ( (!strncasecmp(c, "http://", 7) || (!strncasecmp(c, "https://", 8)))) {
                        t = c;
                        while(1) {
                                if (badchar(*t)) {

					if (*(t) == ',' && (*(t+1) != ' '))
					{
						t++;
						continue;
					}

                                        if (*(t-1) == '.')
                                                t--;
                                        strncpy(url_buf, c, t-c);
                                        url_buf[t-c] = 0;
                                        cnt += g_snprintf(&text[cnt++], 1024, "<A HREF=\"%s\">%s</A>", url_buf, url_buf);
                                        cnt--;
                                        c = t;
                                        break;
                                }
                                if (!t)
                                        break;
                                t++;

                        }
                } else if (!strncasecmp(c, "www.", 4)) {
                        if (strncasecmp(c, "www..", 5)) {
                        t = c;
                        while(1) {
                                if (badchar(*t)) {
                                        if (t-c == 4) {
                                                break;
                                        }

					if (*(t) == ',' && (*(t+1) != ' '))
					{
						t++;
						continue;
					}
					
                                        if (*(t-1) == '.')
                                                t--;
                                        strncpy(url_buf, c, t-c);
                                        url_buf[t-c] = 0;
                                        cnt += g_snprintf(&text[cnt++], 1024, "<A HREF=\"http://%s\">%s</A>", url_buf, url_buf);
                                        cnt--;
                                        c = t;
                                        break;
                                }
                                if (!t)
                                        break;
                                t++;
                        }
                 }
                } else if (!strncasecmp(c, "ftp://", 6)) {
                        t = c;
                        while(1) {
                                if (badchar(*t)) {
                                        if (*(t-1) == '.')
                                                t--;
                                        strncpy(url_buf, c, t-c);
                                        url_buf[t-c] = 0;
                                        cnt += g_snprintf(&text[cnt++], 1024, "<A HREF=\"%s\">%s</A>", url_buf, url_buf);
                                        cnt--;
                                        c = t;
                                        break;
                                }
                                if (!t)
                                        break;
                                t++;

                        }
                } else if (!strncasecmp(c, "ftp.", 4) ) {
			if (strncasecmp(c, "ftp..", 5)) {
                        t = c;
                        while(1) {
                                if (badchar(*t)) {
                                        if (t-c == 4) {
                                                break;
                                        }
                                        if (*(t-1) == '.')
                                                t--;
                                        strncpy(url_buf, c, t-c);
                                        url_buf[t-c] = 0;
                                        cnt += g_snprintf(&text[cnt++], 1024, "<A HREF=\"ftp://%s\">%s</A>", url_buf, url_buf);
                                        cnt--; 
                                        c = t;
                                        break;
                                }
                                if (!t)
                                        break;
                                t++;
                        }
			}
                } else if (!strncasecmp(c, "mailto:", 7)) {
                        t = c;
                        while(1) {
                                if (badchar(*t)) {
                                        if (*(t-1) == '.')
                                                t--;
                                        strncpy(url_buf, c, t-c);
                                        url_buf[t-c] = 0;
                                        cnt += g_snprintf(&text[cnt++], 1024, "<A HREF=\"%s\">%s</A>", url_buf, url_buf);
                                        cnt--;
                                        c = t;
                                        break;
                                }
                                if (!t)
                                        break;
                                t++;

                        }
                } else if (!strncasecmp(c, "@", 1)) {
                        char *tmp;
                        int flag;
                        int len=0;
			char illegal_chars[] = "!@#$%^&*()[]{}/\\<>\":;\0";
                        url_buf[0] = 0;

                        if (*(c-1) == ' ' || *(c+1) == ' ' || rindex(illegal_chars, *(c+1)) || *(c+1) == 13 || *(c+1) == 10)
                                flag = 0;
                        else
                                flag = 1;

                        t = c;
                        while(flag) {
                                if (badchar(*t)) {
                                        cnt -= (len - 1);
                                        break;
                                } else {
                                        len++;
                                        tmp = g_malloc(len + 1);
                                        tmp[len] = 0;
                                        tmp[0] = *t;
                                        strncpy(tmp + 1, url_buf, len - 1);
                                        strcpy(url_buf, tmp);
                                        url_buf[len] = 0;
                                        g_free(tmp);
                                        t--;
                                        if (t < cpy) {
                                                cnt = 0;
                                                break;
                                        }
                                }
                        }


                        t = c + 1;

                        while(flag) {
                                if (badchar(*t)) {
                                        if (*(t-1) == '.')
                                                t--;
                                        cnt += g_snprintf(&text[cnt++], 1024, "<A HREF=\"mailto:%s\">%s</A>", url_buf, url_buf);
                                        text[cnt]=0;


                                        cnt--;
                                        c = t;

                                        break;
                                } else {
                                        strncat(url_buf, t, 1);
                                        len++;
                                        url_buf[len] = 0;
                                }

                                t++;

                        }


                }

                if (*c == 0)
                        break;

                text[cnt++] = *c;
                c++;

        }
        text[cnt]=0;
	g_free(cpy);
        return cnt; 
}


FILE *open_log_file (char *name)
{
        char *buf;
        char *buf2;
        char log_all_file[256];
        struct log_conversation *l;
        struct stat st;
        int flag = 0;
        FILE *fd;
        int res;

        if (!(general_options & OPT_GEN_LOG_ALL)) {

                l = find_log_info(name);
                if (!l)
                        return NULL;

                if (stat(l->filename, &st) < 0)
                        flag = 1;

                fd = fopen(l->filename, "a");

                if (flag) { /* is a new file */
                        fprintf(fd, "<HTML><HEAD><TITLE>" );
                        fprintf(fd, "IM Sessions with %s", name );
                        fprintf(fd, "</TITLE></HEAD><BODY BGCOLOR=\"ffffff\">\n" );
                }

                return fd;
        }

	buf = g_malloc(BUF_LONG);
	buf2 = g_malloc(BUF_LONG);

        /*  Dont log yourself */
        g_snprintf(log_all_file, 256, "%s/.gaim", getenv("HOME"));

        stat(log_all_file, &st);
        if (!S_ISDIR(st.st_mode))
                unlink(log_all_file);

        fd = fopen(log_all_file, "r");

        if (!fd) {
                res = mkdir(log_all_file, S_IRUSR | S_IWUSR | S_IXUSR);
                if (res < 0) {
                        g_snprintf(buf, BUF_LONG, "Unable to make directory %s for logging", log_all_file);
                        do_error_dialog(buf, "Error!");
                        g_free(buf);
                        g_free(buf2);
                        return NULL;
                }
        } else
                fclose(fd);

        g_snprintf(log_all_file, 256, "%s/.gaim/logs", getenv("HOME"));

        if (stat(log_all_file, &st) < 0)
                flag = 1;
        if (!S_ISDIR(st.st_mode))
                unlink(log_all_file);

        fd = fopen(log_all_file, "r");
        if (!fd) {
                res = mkdir(log_all_file, S_IRUSR | S_IWUSR | S_IXUSR);
                if (res < 0) {
                        g_snprintf(buf, BUF_LONG, "Unable to make directory %s for logging", log_all_file);
                        do_error_dialog(buf, "Error!");
                        g_free(buf);
                        g_free(buf2);
                        return NULL;
                }
        } else
                fclose(fd);

        
        g_snprintf(log_all_file, 256, "%s/.gaim/logs/%s.log", getenv("HOME"), normalize(name));

        if (stat(log_all_file, &st) < 0)
                flag = 1;

        sprintf(debug_buff,"Logging to: \"%s\"\n", log_all_file);
        debug_print(debug_buff);

        fd = fopen(log_all_file, "a");

        if (fd && flag) { /* is a new file */
		fprintf(fd, "<HTML><HEAD><TITLE>" );
		fprintf(fd, "IM Sessions with %s", name );
		fprintf(fd, "</TITLE></HEAD><BODY BGCOLOR=\"ffffff\">\n" );
        }

	g_free(buf);
	g_free(buf2);
        return fd;
}


/* we only need this for TOC, because messages must be escaped */
int escape_message(char *msg)
{
	char *c, *cpy;
	int cnt=0;
	/* Assumes you have a buffer able to cary at least BUF_LEN * 2 bytes */
	if (strlen(msg) > BUF_LEN) {
		sprintf(debug_buff, "Warning:  truncating message to 2048 bytes\n");
		debug_print(debug_buff);
		msg[2047]='\0';
	}
	
	cpy = g_strdup(msg);
	c = cpy;
	while(*c) {
		switch(*c) {
		case '$':
		case '[':
		case ']':
		case '(':
		case ')':
		case '#':
			msg[cnt++]='\\';
			/* Fall through */
		default:
			msg[cnt++]=*c;
		}
		c++;
	}
	msg[cnt]='\0';
	g_free(cpy);
	return cnt;
}

/* we don't need this for oscar either */
int escape_text(char *msg)
{
	char *c, *cpy;
	int cnt=0;
	/* Assumes you have a buffer able to cary at least BUF_LEN * 4 bytes */
	if (strlen(msg) > BUF_LEN) {
		fprintf(stderr, "Warning:  truncating message to 2048 bytes\n");
		msg[2047]='\0';
	}
	
	cpy = g_strdup(msg);
	c = cpy;
	while(*c) {
		switch(*c) {
		case '\n':
			msg[cnt++] = '<';
			msg[cnt++] = 'B';
			msg[cnt++] = 'R';
			msg[cnt++] = '>';
			break;
		case '{':
		case '}':
		case '\\':
		case '"':
			msg[cnt++]='\\';
			/* Fall through */
		default:
			msg[cnt++]=*c;
		}
		c++;
	}
	msg[cnt]='\0';
	g_free(cpy);
	return cnt;
}

char * escape_text2(char *msg)
{
        char *c, *cpy;
	char *woo;
        int cnt=0;
        /* Assumes you have a buffer able to cary at least BUF_LEN * 2 bytes */
        if (strlen(msg) > BUF_LEN) {
                fprintf(stderr, "Warning:  truncating message to 2048 bytes\n");
                msg[2047]='\0';
        }

	woo = malloc(strlen(msg) * 2); 
        cpy = g_strdup(msg);
        c = cpy;
        while(*c) {
                switch(*c) {
                case '\n':
                        woo[cnt++] = '<';
                        woo[cnt++] = 'B';
                        woo[cnt++] = 'R';
                        woo[cnt++] = '>';
                        break;
                case '{':
                case '}':
                case '\\':
                case '"':
                        woo[cnt++]='\\';
                        /* Fall through */
                default:
                        woo[cnt++]=*c;
                }
                c++;
        }
        woo[cnt]='\0';

	g_free (cpy);
        return woo;
}


char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                  "0123456789+/";
                  

char *tobase64(char *text)
{
	char *out = NULL;
	char *c;
	unsigned int tmp = 0;
	int len = 0, n = 0;
	
	c = text;
	
	while(c) {
		tmp = tmp << 8;
		tmp += *c;
		n++;

		if (n == 3) {
			out = g_realloc(out, len+4);
			out[len] = alphabet[(tmp >> 18) & 0x3f];
			out[len+1] = alphabet[(tmp >> 12) & 0x3f];
			out[len+2] = alphabet[(tmp >> 6) & 0x3f];
			out[len+3] = alphabet[tmp & 0x3f];
			len += 4;
			tmp = 0;
			n = 0;
		}
		c++;
	}
	switch(n) {
	
	case 2:
	        out = g_realloc(out, len+5);
	        out[len] = alphabet[(tmp >> 12) & 0x3f];
	        out[len+1] = alphabet[(tmp >> 6) & 0x3f];
	        out[len+2] = alphabet[tmp & 0x3f];
	        out[len+3] = '=';
	        out[len+4] = 0;
	        break;
	case 1:
	        out = g_realloc(out, len+4);
	        out[len] = alphabet[(tmp >> 6) & 0x3f];
	        out[len+1] = alphabet[tmp & 0x3f];
	        out[len+2] = '=';
	        out[len+3] = 0;
	        break;
	case 0:
		out = g_realloc(out, len+2);
		out[len] = '=';
		out[len+1] = 0;
		break;
	}
	return out;
}


char *frombase64(char *text)
{
        char *out = NULL;
        char tmp = 0;
        char *c;
        gint32 tmp2 = 0;
        int len = 0, n = 0;
        
        c = text;
        
        while(*c) {
                if (*c >= 'A' && *c <= 'Z') {
                        tmp = *c - 'A';
                } else if (*c >= 'a' && *c <= 'z') {
                        tmp = 26 + (*c - 'a');
                } else if (*c >= '0' && *c <= 57) {
                        tmp = 52 + (*c - '0');
                } else if (*c == '+') {
                        tmp = 62;
                } else if (*c == '/') {
                	tmp = 63;
                } else if (*c == '=') {
                        if (n == 3) {
                                out = g_realloc(out, len + 2);
                                out[len] = (char)(tmp2 >> 10) & 0xff;
                                len++;
                                out[len] = (char)(tmp2 >> 2) & 0xff;
                                len++;
                        } else if (n == 2) {
                                out = g_realloc(out, len + 1);
                                out[len] = (char)(tmp2 >> 4) & 0xff;
                                len++;
                        }
                        break;
                }
                tmp2 = ((tmp2 << 6) | (tmp & 0xff));
                n++;
                if (n == 4) {
                        out = g_realloc(out, len + 3);
                        out[len] = (char)((tmp2 >> 16) & 0xff);
                        len++;
                        out[len] = (char)((tmp2 >> 8) & 0xff);
                        len++;
                        out[len] = (char)(tmp2 & 0xff);
                        len++;
                        tmp2 = 0;
                        n = 0;
                }
                c++;
        }

        out = g_realloc(out, len+1);
        out[len] = 0;
        
        return out;
}


char *normalize(const char *s)
{
	static char buf[BUF_LEN];
        char *t, *u;
        int x=0;

        g_return_val_if_fail ((s != NULL), NULL);

        u = t = g_strdup(s);

        strcpy(t, s);
        g_strdown(t);
        
	while(*t && (x < BUF_LEN - 1)) {
		if (*t != ' ') {
			buf[x] = *t;
			x++;
		}
		t++;
	}
        buf[x]='\0';
        g_free(u);
	return buf;
}

char *date()
{
	static char date[80];
	time_t tme;
	time(&tme);
	strftime(date, sizeof(date), "%H:%M:%S", localtime(&tme));
	return date;
}


gint clean_pid(void *dummy)
{
	int status;
	pid_t pid;

	pid = waitpid(-1, &status, WNOHANG);

	if (pid == 0)
		return TRUE;

	return FALSE;
}

void aol_icon(GdkWindow *w)
{
#ifndef _WIN32
	if (icon_pm == NULL) {
		icon_pm = gdk_pixmap_create_from_xpm_d(w, &icon_bm,
						       NULL, (gchar **)aimicon_xpm);
	}
	gdk_window_set_icon(w, NULL, icon_pm, icon_bm);
	if (mainwindow) gdk_window_set_group(w, mainwindow->window);
#endif
}

struct aim_user *find_user(const char *name)
{
        char *who = g_strdup(normalize(name));
        GList *usr = aim_users;
        struct aim_user *u;

        while(usr) {
                u = (struct aim_user *)usr->data;
                if (!strcmp(normalize(u->username), who)) {
                        g_free(who);
                        return u;
                }
                usr = usr->next;
        }
        g_free(who);
        return NULL;
}

void check_gaim_versions()
{
	char *cur_ver;
	char *tmp;

	tmp = (char *)malloc(BUF_LONG);

	cur_ver = (char *)grab_url("http://www.marko.net/gaim/latest-gaim");

	if (!strncasecmp(cur_ver, "g00", 3))
	{
		g_free(cur_ver);
		g_free(tmp);
		return;
	}

	g_snprintf(tmp, BUF_LONG, "%s", strstr(cur_ver, "plain")+9);
	g_strchomp(tmp);

	if (strcmp(tmp, latest_ver))
	{
		g_snprintf(cur_ver, BUF_LONG, "GAIM v%s is now available.\n\nDownload it at http://www.marko.net/gaim\n", tmp);
	
		do_error_dialog(cur_ver, "GAIM - New Version!");
		strcpy(latest_ver, tmp);
		save_prefs();
	}

	free(tmp);
	free(cur_ver);
}


/*  

This function was taken from EveryBuddy and was written by
Torrey Searle.  tsearle@valhalla.marko.net 

http://www.everybuddy.com

*/

void spell_checker(GtkWidget * text)
{
        int start = 0;
        int end = 0;
        static GdkColor * color = NULL;
        int ignore = 0;
        int point = gtk_editable_get_position(GTK_EDITABLE(text));

        GString * string;

        if( color == NULL )
        {
                GdkColormap * gc = gtk_widget_get_colormap( text );
                color = g_new0(GdkColor, 1);
                color->red = 255 * 256;
                gdk_colormap_alloc_color(gc, color, FALSE, TRUE);
        }



        end = point-1;
        for( start = end-1; start >= 0; start-- )
        {
                if((isspace(GTK_TEXT_INDEX(GTK_TEXT(text), start)) || start == 0)
                                && !ignore)
                {
                        char * word;
                        FILE * file;
                        char buff[1024];
                        word = gtk_editable_get_chars( GTK_EDITABLE(text), start, end );
                        g_snprintf(buff, 1024, "echo \"%s\" | ispell -l", word );
                        file = popen(buff, "r");

                        buff[0] = 0;
                        fgets(buff, 255, file);

                        if(strlen(buff) > 0 )
                        {
                                string = g_string_new(word);
                                gtk_text_set_point(GTK_TEXT(text), end);
                                gtk_text_backward_delete(GTK_TEXT(text), end-start);
                                gtk_text_insert( GTK_TEXT(text), NULL, color, NULL,
                                                                 string->str, string->len );
                                g_string_free( string, TRUE );
                        }
                        else
                        {
                                string = g_string_new(word);
                                gtk_text_set_point(GTK_TEXT(text), end);
                                gtk_text_backward_delete(GTK_TEXT(text), end-start);
                                gtk_text_insert( GTK_TEXT(text), NULL, &(text->style->fg[0]), NULL,
                                                                 string->str, string->len );
                                g_string_free( string, TRUE );
                        }
                        pclose( file);
			g_free(word);
                        break;
                }
                else if(!isalpha(GTK_TEXT_INDEX(GTK_TEXT(text), start)))
                {
                        ignore = 1;
                }
        }
        gtk_text_set_point(GTK_TEXT(text), point);

}

/* Look for %n, %d, or %t in msg, and replace with the sender's name, date,
   or time */
char *away_subs(char *msg, char *name)
{
	char *c;
	static char cpy[BUF_LONG];
	int cnt=0;
	time_t t = time(0);
	struct tm *tme = localtime(&t);
	char tmp[20];

	cpy[0] = '\0';
	c = msg;
	while(*c) {
		switch(*c) {
		case '%':
			if (*(c+1)) {
				switch (*(c+1)) {
				case 'n':
					// append name
					strcpy (cpy+cnt, name);
					cnt += strlen(name);
					c++;
					break;
				case 'd':
					// append date
					strftime (tmp, 20, "%D", tme);
					strcpy (cpy+cnt, tmp);
					cnt += strlen(tmp);
					c++;
					break;
				case 't':
					// append time
					strftime (tmp, 20, "%r", tme);
					strcpy (cpy+cnt, tmp);
					cnt += strlen(tmp);
					c++;
					break;
				default:
					cpy[cnt++]=*c;
				}
			}
			break;
		default:
			cpy[cnt++]=*c;
		}
		c++;
	}
	cpy[cnt]='\0';
	return(cpy);
}

GtkWidget *picture_button(GtkWidget *window, char *text, char **xpm)
{
	GtkWidget *button;
	GtkWidget *button_box, *button_box_2, *button_box_3;
	GtkWidget *label;
	GdkBitmap *mask;
	GdkPixmap *pm;
	GtkWidget *pixmap;

	button = gtk_button_new();
	if (display_options & OPT_DISP_COOL_LOOK)
		gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);

	button_box = gtk_hbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(button), button_box);

	button_box_2 = gtk_hbox_new(FALSE, 0);
	button_box_3 = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(button_box), button_box_2, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(button_box), button_box_3, TRUE, TRUE, 0);
	pm = gdk_pixmap_create_from_xpm_d(window->window, &mask, NULL, xpm);
	pixmap = gtk_pixmap_new(pm, mask);
	gtk_box_pack_end(GTK_BOX(button_box_2), pixmap, FALSE, FALSE, 0);

	if (text)
	{
		label = gtk_label_new(text);
		gtk_box_pack_start(GTK_BOX(button_box_3), label, FALSE, FALSE, 2);
		gtk_widget_show(label);
	}

	gtk_widget_show(pixmap);
	gtk_widget_show(button_box_2);
	gtk_widget_show(button_box_3);
	gtk_widget_show(button_box);

/* this causes clipping on lots of buttons with long text */
/*	gtk_widget_set_usize(button, 75, 30);*/
	gtk_widget_show(button);
	gdk_pixmap_unref(pm);
	gdk_bitmap_unref(mask);
		
	return button;
}

static GtkTooltips *tips = NULL;
GtkWidget *picture_button2(GtkWidget *window, char *text, char **xpm, short dispstyle)
{
	GtkWidget *button;
	GtkWidget *button_box, *button_box_2;
	GdkBitmap *mask;
	GdkPixmap *pm;
	GtkWidget *pixmap;
	GtkWidget *label;
	
	if (!tips) tips = gtk_tooltips_new();
	button = gtk_button_new();
	if (display_options & OPT_DISP_COOL_LOOK)
		gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);

	button_box = gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(button), button_box);

	button_box_2 = gtk_vbox_new(FALSE, 0);

	gtk_box_pack_start(GTK_BOX(button_box), button_box_2, TRUE, TRUE, 0);
	gtk_widget_show(button_box_2);
	gtk_widget_show(button_box);
	if (dispstyle == 2 || dispstyle == 0) {
		pm = gdk_pixmap_create_from_xpm_d(window->window, &mask, NULL, xpm);
		pixmap = gtk_pixmap_new(pm, mask);
		gtk_box_pack_start(GTK_BOX(button_box_2), pixmap, FALSE, FALSE, 0);

		gtk_widget_show(pixmap);

		gdk_pixmap_unref(pm);
		gdk_bitmap_unref(mask);
	}

	if (dispstyle == 2 || dispstyle == 1)
	{
		label = gtk_label_new(text);
		gtk_widget_show(label);
		gtk_box_pack_end(GTK_BOX(button_box_2), label, FALSE, FALSE, 0);
	}

	gtk_tooltips_set_tip(tips, button, text, "Gaim");	
	gtk_widget_show(button);
	return button;
}


/* remove leading whitespace from a string */
char *remove_spaces (char *str)
{
	int i;
	char *new;

	if (str == NULL)
		return NULL;

	i = strspn (str, " \t\n\r\f");
	new = &str[i];

	return new;
}


/* translate an AIM 3 buddylist (*.lst) to a GAIM buddylist */
void translate_lst (FILE *src_fp, char *dest) 
{
	char line[BUF_LEN], *line2;
	char *name;
	int i;

	sprintf (dest, "m 1\n");
  
	while (fgets (line, BUF_LEN, src_fp)) {
		line2 = remove_spaces (line);
		if (strstr (line2, "group") == line2) {
			name = strpbrk (line2, " \t\n\r\f") + 1;
			strcat (dest, "g ");
			for (i = 0; i < strcspn (name, "\n\r"); i++)
				if (name[i] != '\"')
					strncat (dest, &name[i], 1);
			strcat (dest, "\n");
		}
		if (strstr (line2, "buddy") == line2) {
			name = strpbrk (line2, " \t\n\r\f") + 1;
			strcat (dest, "b ");
			for (i = 0; i < strcspn (name, "\n\r"); i++)
				if (name[i] != '\"')
					strncat (dest, &name[i], 1);
			strcat (dest, "\n");
		}
	}

	return;
}


/* translate an AIM 4 buddylist (*.blt) to GAIM format */
void translate_blt (FILE *src_fp, char *dest)
{
	int i;
	char line[BUF_LEN];
	char *buddy;

	sprintf (dest, "m 1\n");
  
	while (strstr (fgets (line, BUF_LEN, src_fp), "Buddy") == NULL)	;
	while (strstr (fgets (line, BUF_LEN, src_fp), "list") == NULL)	;

	while (1) {
		fgets (line, BUF_LEN, src_fp);
		if (strchr (line, '}') != NULL)
			break;
    
		/* Syntax starting with "<group> {" */
		if (strchr (line, '{') != NULL) {
			strcat (dest, "g ");
			buddy = remove_spaces (strtok (line, "{"));
			for (i = 0; i < strlen(buddy); i++) {
				if (buddy[i] != '\"')
					strncat (dest, &buddy[i], 1);
			}
			strcat (dest, "\n");
			while (strchr (fgets (line, BUF_LEN, src_fp), '}') == NULL) {
				buddy = remove_spaces (line);
				strcat (dest, "b ");
				if (strchr (buddy, '\"') != NULL) {
					strncat (dest, &buddy[1], strlen(buddy)-3);
					strcat (dest, "\n");
				} else
					strcat (dest, buddy);
			}
		}
		/* Syntax "group buddy buddy ..." */
		else {
			buddy = remove_spaces (strtok (line, " \n"));
			strcat (dest, "g ");
			if (strchr (buddy, '\"') != NULL) {
				strcat (dest, &buddy[1]);
				strcat (dest, " ");
				buddy = remove_spaces (strtok (NULL, " \n"));
				while (strchr (buddy, '\"') == NULL) {
					strcat (dest, buddy);
					strcat (dest, " ");
					buddy = remove_spaces (strtok (NULL, " \n"));
				}
				strncat (dest, buddy, strlen(buddy)-1);
			} else {
				strcat (dest, buddy);
			}
			strcat (dest, "\n");
			while ((buddy = remove_spaces (strtok (NULL, " \n"))) != NULL) {
				strcat (dest, "b ");
				if (strchr (buddy, '\"') != NULL) {
					strcat (dest, &buddy[1]);
					strcat (dest, " ");
					buddy = remove_spaces (strtok (NULL, " \n"));
					while (strchr (buddy, '\"') == NULL) {
						strcat (dest, buddy);
						strcat (dest, " ");
						buddy = remove_spaces (strtok (NULL, " \n"));
					}
					strncat (dest, buddy, strlen(buddy)-1);
				} else {
					strcat (dest, buddy);
				}
				strcat (dest, "\n");
			}
		}
	}

	return;
}

char *stylize(gchar *text, int length)
{
	gchar *buf;	
	char tmp[length];

	buf = g_malloc(length);
	g_snprintf(buf, length, "%s", text);

	if (font_options & OPT_FONT_BOLD) {
		g_snprintf(tmp, length, "<B>%s</B>", buf);
		strcpy(buf, tmp);
	}

	if (font_options & OPT_FONT_ITALIC) {
		g_snprintf(tmp, length, "<I>%s</I>", buf);
		strcpy(buf, tmp);
	}

	if (font_options & OPT_FONT_UNDERLINE) {
		g_snprintf(tmp, length, "<U>%s</U>", buf);
		strcpy(buf, tmp);
	}

	if (font_options & OPT_FONT_STRIKE) {
		g_snprintf(tmp, length, "<S>%s</S>", buf);
		strcpy(buf, tmp);
	}

	if (font_options & OPT_FONT_FACE) {
		g_snprintf(tmp, length, "<FONT FACE=\"%s\">%s</FONT>", fontface, buf);
		strcpy(buf, tmp);
	}

	if (font_options & OPT_FONT_FGCOL) {
		g_snprintf(tmp, length, "<FONT COLOR=\"#%02X%02X%02X\">%s</FONT>", fgcolor.red, fgcolor.green, fgcolor.blue, buf);
		strcpy(buf, tmp);
	}

	if (font_options & OPT_FONT_BGCOL) {
		g_snprintf(tmp, length, "<BODY BGCOLOR=\"#%02X%02X%02X\">%s</BODY>", bgcolor.red, bgcolor.green, bgcolor.blue, buf);
		strcpy(buf, tmp);
	}
	
	return buf;
}

int set_dispstyle (int chat)
{
	int dispstyle;

	if (chat) {
		switch (display_options & (OPT_DISP_CHAT_BUTTON_TEXT | 
					   OPT_DISP_CHAT_BUTTON_XPM)) {
		case OPT_DISP_CHAT_BUTTON_TEXT:
			  dispstyle = 1;
			  break;
		case OPT_DISP_CHAT_BUTTON_XPM:
			  dispstyle = 0;
			  break;
		default: /* both or neither */
			  dispstyle = 2;
			  break;
		}
	} else {
		switch (display_options & (OPT_DISP_CONV_BUTTON_TEXT | 
					   OPT_DISP_CONV_BUTTON_XPM)) {
		case OPT_DISP_CONV_BUTTON_TEXT:
			  dispstyle = 1;
			  break;
		case OPT_DISP_CONV_BUTTON_XPM:
			  dispstyle = 0;
			  break;
		default: /* both or neither */
			  dispstyle = 2;
			  break;
		}
	}
	return dispstyle;
}
