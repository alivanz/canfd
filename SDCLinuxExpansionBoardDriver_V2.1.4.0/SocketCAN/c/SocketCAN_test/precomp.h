/**
 * @file precomp.h
 * @brief header precompile
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2022, SUNIX Co., Ltd.
 *
 * Authors      : Andy Jheng <andy_jheng@sunix.com>
 *
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <termios.h>
#include <dirent.h>
#include <ctype.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/wait.h>

#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <sys/socket.h>

#include "global.h"
#include "function.h"