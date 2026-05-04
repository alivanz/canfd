// SPDX-License-Identifier: MIT
/*
 * SDC library
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

#include "precomp.h"

static void _create_obj(struct dio_ex *ex, int line, int fd)
{
	xlist_init(&ex->entry);
	ex->line = line;
	ex->fd = fd;
	ex->rxthd_create = 0;
	ex->rxthd_exit_fd = 0;

	xlist_insert_tail(&GB_dio_ex_list, &ex->entry);
}

static void _terminate_obj(struct dio_ex *ex)
{
	xlist_remove_entry(&ex->entry);
}

struct dio_ex *dio_create_one(int line, int fd)
{
	struct dio_ex *ex = NULL;

	ex = malloc(sizeof(*ex));
	if (!ex)
		return NULL;

	memset(ex, 0, sizeof(*ex));

	_create_obj(ex, line, fd);

	GB_nr_of_dio_ex++;

	if (ex)
		XPRINT("DIO(%d), CREATE OK\n", ex->line);
	else
		XPRINT("DIO(%d), CREATE FAIL\n", line);

	return ex;
}

void dio_terminate_one(int line)
{
	struct dio_ex *ex;
	struct xlist *entry;

	entry = GB_dio_ex_list.next;
	while (entry && entry != &GB_dio_ex_list) {
		ex = DIO_EX_PTR(entry);
		if (ex && ex->line == line) {
			XPRINT("DIO(%d), TERMINATE\n", ex->line);

			_terminate_obj(ex);
			free(ex);
			ex = NULL;

			GB_nr_of_dio_ex--;
			break;
		}
		entry = entry->next;
	}
}

void dio_terminate_all(void)
{
	struct dio_ex *ex;
	struct xlist *entry;

	while (!xlist_empty(&GB_dio_ex_list)) {
		entry = xlist_get_next(&GB_dio_ex_list);
		if (entry) {
			ex = DIO_EX_PTR(entry);
			if (ex) {
				XPRINT("DIO(%d), TERMINATE\n", ex->line);

				_terminate_obj(ex);
				free(ex);
				ex = NULL;

				GB_nr_of_dio_ex--;
			}
		}
	}
}

struct dio_ex *dio_get_by_line(int line)
{
	struct dio_ex *ex;
	struct xlist *entry;

	entry = GB_dio_ex_list.next;
	while (entry && entry != &GB_dio_ex_list) {
		ex = DIO_EX_PTR(entry);
		if (ex && ex->line == line)
			return ex;
		entry = entry->next;
	}

	return NULL;
}
