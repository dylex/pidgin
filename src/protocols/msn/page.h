/**
 * @file page.h Paging functions
 *
 * gaim
 *
 * Copyright (C) 2003-2004 Christian Hammond <chipx86@gnupdate.org>
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
#ifndef _MSN_PAGE_H_
#define _MSN_PAGE_H_

typedef struct _MsnPage MsnPage;

#include "session.h"
#include "user.h"

/**
 * A page.
 */
struct _MsnPage
{
	char *from_location;
	char *from_phone;

	size_t size;

	char *body;
};

/**
 * Creates a new, empty page.
 *
 * @return A new page.
 */
MsnPage *msn_page_new(void);

/**
 * Destroys a page.
 */
void msn_page_destroy(MsnPage *page);

/**
 * Converts a page to a payload string.
 *
 * @param page     The page.
 * @param ret_size The returned size of the payload.
 *
 * @return The payload string of a page.
 */
char *msn_page_gen_payload(const MsnPage *page, size_t *ret_size);

/**
 * Sets the body of a page.
 *
 * @param page  The page.
 * @param body The body of the page.
 */
void msn_page_set_body(MsnPage *page, const char *body);

/**
 * Returns the body of the page.
 *
 * @param page The page.
 *
 * @return The body of the page.
 */
const char *msn_page_get_body(const MsnPage *page);

#endif /* _MSN_PAGE_H_ */
