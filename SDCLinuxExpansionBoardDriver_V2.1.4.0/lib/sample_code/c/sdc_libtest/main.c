// SPDX-License-Identifier: MIT
/*
 * SDC library test program
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

static void _print_cmd(void)
{
	printf(">--------------------------------------<\n");
	printf("     E : DIO test\n");
	printf("\n");
	printf("     Q : quit\n");
	printf(">--------------------------------------<\n\n");
}

int main(void)
{
	int cmd;

	(void)!system("clear");
	_print_cmd();

	do {
		printf("\n> CMD ?\n");
		cmd = sgetche();
		printf("\n");

		switch (cmd) {
		case 'e':
		case 'E':
			dio_test();
			break;
		case '?':
			_print_cmd();
			break;
		default:
			break;
		}

	} while (cmd != 'q' && cmd != 'Q');

	return 0;
}
