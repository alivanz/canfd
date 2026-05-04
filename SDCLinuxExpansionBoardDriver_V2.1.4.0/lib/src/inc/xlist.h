/* SPDX-License-Identifier: MIT */
/*
 * link list header
 *
 * Copyright (c) 2025 SUNIX Co., Ltd. <info@sunix.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _XLIST_H
#define _XLIST_H

#include <stddef.h>

struct xlist {
	struct xlist *prev;
	struct xlist *next;
};

#define CONTAIN_OF(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

static inline void xlist_init(struct xlist *head)
{
	head->next = head->prev = head;
}

static inline int xlist_empty(struct xlist *head)
{
	return head->next == head;
}

static inline struct xlist *xlist_get_next(struct xlist *head)
{
	return head->next;
}

static inline void xlist_remove_entry(struct xlist *entry)
{
	struct xlist *prev = entry->prev;
	struct xlist *next = entry->next;

	prev->next = next;
	next->prev = prev;
}

static inline void xlist_insert_tail(struct xlist *head, struct xlist *entry)
{
	struct xlist *prev = head->prev;

	entry->next = head;
	head->prev = entry;
	entry->prev = prev;
	prev->next = entry;
}

#endif
