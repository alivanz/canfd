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

static void *_rxthd(void *context)
{
	struct dio_ex *ex = (struct dio_ex *)context;
	struct dio_read r;
	struct timeval *timeout, to;
	fd_set fds;
	int exit_thd = 0, max_fd, ret, input, input_delta;

	XPRINT("DIO(%d), RX thd, START\n", ex->line);

	while (1) {

		FD_ZERO(&fds);
		FD_SET(ex->rxthd_exit_fd, &fds);
		FD_SET(ex->fd, &fds);

		max_fd = 0;
		if (ex->rxthd_exit_fd > max_fd)
			max_fd = ex->rxthd_exit_fd;
		if (ex->fd > max_fd)
			max_fd = ex->fd;

		if (ex->rxthd_wait_timeout > 0) {
			to.tv_sec = ex->rxthd_wait_timeout / 1000;
			to.tv_usec = (ex->rxthd_wait_timeout % 1000) * 1000;
			timeout = &to;
		} else {
			memset(&to, 0, sizeof(to));
			timeout = NULL;
		}

		ret = select(max_fd + 1, &fds, NULL, NULL, timeout);
		XPRINT("DIO(%d), RX thd, after select, ret:%d\n", ex->line, ret);

		if (ret < 0) {
			XPRINT("DIO(%d), RX thd, select fail, errno:%d\n", ex->line, errno);
		} else if (ret == 0) {
			XPRINT("DIO(%d), RX thd, select timeout\n", ex->line);
		} else {
			if (FD_ISSET(ex->rxthd_exit_fd, &fds)) {
				XPRINT("DIO(%d), RX thd, FD_ISSET, rxthd_exit_fd\n", ex->line);
				exit_thd = 1;
			} else if (FD_ISSET(ex->fd, &fds)) {
				if (read(ex->fd, &r, sizeof(r)) == sizeof(r)) {
					XPRINT("DIO(%d), RX thd, FD_ISSET, fd, input:x%08X, delta:x%08X\n",
						ex->line, r.input, r.input_delta);
					pthread_mutex_lock(&ex->callback_lock);
					if (ex->callback_ptr) {
						input = (int)r.input >> ex->drvinfo.banks[ex->callback_bank_idx].io_shift_bits;
						input_delta = (int)r.input_delta >> ex->drvinfo.banks[ex->callback_bank_idx].io_shift_bits;

						ex->callback_ptr(ex->line, ex->callback_bank_idx, input, input_delta);
					}
					pthread_mutex_unlock(&ex->callback_lock);
				}
			}
		}

		if (exit_thd)
			break;
	}

	XPRINT("DIO(%d), RX thd, EXIT\n", ex->line);
	pthread_exit(NULL);
}

int dio_rxthd_create(struct dio_ex *ex)
{
	int ret;
	int step_0 = 0;
	int step_1 = 0;
	int status = -1;

	XPRINT("----> %s(), line:%d\n", __func__, ex->line);

	do {
		if (ex->rxthd_create) {
			status = 0;
			break;
		}

		ex->rxthd_event_count = 0;

		ex->rxthd_exit_fd = eventfd(0, 0);
		if (ex->rxthd_exit_fd < 0)
			break;
		step_0 = 1;

		ret = pthread_attr_init(&ex->rxthd_attr);
		if (ret < 0)
			break;
		step_1 = 1;

		ret = pthread_attr_setstacksize(&ex->rxthd_attr, THD_STACK_SIZE);
		if (ret < 0)
			break;

		ret = pthread_create(&ex->rxthd_hd, &ex->rxthd_attr, _rxthd, (void *)ex);
		if (ret < 0)
			break;

		ex->rxthd_create = 1;
		status = 0;

	} while (0);

	if (status != 0) {
		if (step_1)
			pthread_attr_destroy(&ex->rxthd_attr);

		if (step_0) {
			close(ex->rxthd_exit_fd);
			ex->rxthd_exit_fd = -1;
		}
	}

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

void dio_rxthd_terminate(struct dio_ex *ex)
{
	uint64_t data = 1;

	XPRINT("----> %s(), line:%d\n", __func__, ex->line);

	do {
		if (!ex->rxthd_create) {
			XPRINT("%s - RX thd, not created\n", __func__);
			break;
		}

		(void)!write(ex->rxthd_exit_fd, &data, sizeof(data));

		pthread_join(ex->rxthd_hd, NULL);
		pthread_attr_destroy(&ex->rxthd_attr);

		close(ex->rxthd_exit_fd);
		ex->rxthd_exit_fd = -1;

		ex->rxthd_create = 0;

	} while (0);

	XPRINT("<---- %s\n", __func__);
}
