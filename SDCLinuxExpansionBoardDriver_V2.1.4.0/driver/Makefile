# SPDX-License-Identifier: GPL-2.0
#
# SUNIX SDC driver Makefile
#
# Copyright (c) 2025 SUNIX Co., Ltd. <info@sunix.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
#

KVERDOT = $(word $2,$(subst ., ,$(shell uname -r)))
KMAJ = $(call KVERDOT,$*,1)
KMIN = $(call KVERDOT,$*,2)
KREL = $(call KVERDOT,$*,3)

ENABLE_CREATE_UART = 1
ENABLE_CREATE_DIO = 1
ENABLE_CREATE_SPI = 1
ENABLE_CREATE_CAN = 1
ENABLE_CREATE_PARPORT = 0
ENABLE_CREATE_GPIO = 0

ifeq ($(FSDC_ENABLE_GPIO), 1)
$(eval ENABLE_CREATE_DIO = 0)
$(eval ENABLE_CREATE_GPIO = 1)
endif

DRIVER_DIR_MFD = /lib/modules/$(shell uname -r)/kernel/drivers/mfd
DRIVER_DIR_CHAR = /lib/modules/$(shell uname -r)/kernel/drivers/char
DRIVER_DIR_8250 = /lib/modules/$(shell uname -r)/kernel/drivers/tty/serial/8250
DRIVER_DIR_SPI = /lib/modules/$(shell uname -r)/kernel/drivers/spi
DRIVER_DIR_SPI_CAN = /lib/modules/$(shell uname -r)/kernel/drivers/net/can/spi
DRIVER_DIR_CAN = /lib/modules/$(shell uname -r)/kernel/drivers/net/can
DRIVER_DIR_GPIO = /lib/modules/$(shell uname -r)/kernel/drivers/gpio

obj-m += sunix-sdc.o
obj-m += sdc_config.o
ifeq ($(ENABLE_CREATE_UART), 1)
obj-m += 8250_sdc.o
endif
ifeq ($(ENABLE_CREATE_DIO), 1)
obj-m += sdc_dio.o
endif
ifeq ($(ENABLE_CREATE_SPI), 1)
obj-m += spi-sdc.o
obj-m += sdc_mcp251x.o
obj-m += sdc_mcp251xfd.o
endif
ifeq ($(ENABLE_CREATE_CAN), 1)
obj-m += can_sdc.o
endif

ifeq ($(ENABLE_CREATE_SPI), 1)
sdc_mcp251x-y := mcp251x/mcp251x.o

sdc_mcp251xfd-y := mcp251xfd/mcp251xfd-core.o \
	mcp251xfd/mcp251xfd-crc16.o \
	mcp251xfd/mcp251xfd-regmap.o
endif

ifeq ($(ENABLE_CREATE_CAN), 1)
can_sdc-y := sdccanfd/sdc_canfd.o
endif

ifeq ($(ENABLE_CREATE_GPIO), 1)
obj-m += gpio-sdc.o
endif

SDCFLAGS+= -DSDC_CREATE_UART=$(ENABLE_CREATE_UART)
SDCFLAGS+= -DSDC_CREATE_DIO=$(ENABLE_CREATE_DIO)
SDCFLAGS+= -DSDC_CREATE_SPI=$(ENABLE_CREATE_SPI)
SDCFLAGS+= -DSDC_CREATE_CAN=$(ENABLE_CREATE_CAN)
SDCFLAGS+= -DSDC_CREATE_PARPORT=$(ENABLE_CREATE_PARPORT)
SDCFLAGS+= -DSDC_CREATE_GPIO=$(ENABLE_CREATE_GPIO)

#PARAMS= CFLAGS_MODULE+=-DNETCAN CFLAGS_MODULE+=-Wframe-larger-than=2048 \
#	EXTRA_CFLAGS+="$(SDCFLAGS)"
ccflags-y += $(SDCFLAGS)
ccflags-y += -DNETCAN
ccflags-y += -Wframe-larger-than=2048


default:
    ifeq ($(KMAJ), $(filter $(KMAJ),2 3))
		@echo "Kernel $(KMAJ).x not supported."
		@exit 1
    endif
    ifeq ($(KMAJ) $(KMIN), 4 $(filter $(KMIN),0 1 2 3 4 5 6 7 8 9 10))
		@echo "Kernel $(KMAJ).$(KMIN) not supported."
		@exit 1
    endif
	@echo "Compile SDC driver. (kernel $(KMAJ).$(KMIN).$(KREL))"
	@echo ""
#	make $(PARAMS) -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	rm -f obj/*
	mv sunix-sdc.ko obj/
	mv sdc_config.ko obj/
    ifeq ($(ENABLE_CREATE_UART), 1)
		mv 8250_sdc.ko obj/
    endif
    ifeq ($(ENABLE_CREATE_DIO), 1)
		mv sdc_dio.ko obj/
    endif
    ifeq ($(ENABLE_CREATE_SPI), 1)
		mv spi-sdc.ko obj/
		mv sdc_mcp251x.ko obj/
		mv sdc_mcp251xfd.ko obj/
    endif
    ifeq ($(ENABLE_CREATE_CAN), 1)
		mv can_sdc.ko obj/
    endif
    ifeq ($(ENABLE_CREATE_GPIO), 1)
		mv gpio-sdc.ko obj/
    endif

clean:
	@echo "Clean SDC driver. (kernel $(KMAJ).$(KMIN).$(KREL))"
	@echo ""
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm -Rf .tmp_versions
	rm -f .cache.*

install:
	@echo "Install SDC driver. (kernel $(KMAJ).$(KMIN).$(KREL))"
	@echo ""
	@sudo mkdir -p $(DRIVER_DIR_MFD)
	@sudo mkdir -p $(DRIVER_DIR_CHAR)
	@sudo mkdir -p $(DRIVER_DIR_8250)
	@sudo mkdir -p $(DRIVER_DIR_SPI)
	@sudo mkdir -p $(DRIVER_DIR_SPI_CAN)
	@sudo mkdir -p $(DRIVER_DIR_CAN)
	@sudo mkdir -p $(DRIVER_DIR_GPIO)
    ifeq ($(shell test -e ./obj/sunix-sdc.ko && echo -n yes),yes)
		sudo cp -f ./obj/sunix-sdc.ko $(DRIVER_DIR_MFD)
    endif
    ifeq ($(shell test -e ./obj/sdc_config.ko && echo -n yes),yes)
		sudo cp -f ./obj/sdc_config.ko $(DRIVER_DIR_CHAR)
    endif
    ifeq ($(shell test -e ./obj/8250_sdc.ko && echo -n yes),yes)
		sudo cp -f ./obj/8250_sdc.ko $(DRIVER_DIR_8250)
    endif
    ifeq ($(shell test -e ./obj/sdc_dio.ko && echo -n yes),yes)
		sudo cp -f ./obj/sdc_dio.ko $(DRIVER_DIR_CHAR)
    endif
    ifeq ($(shell test -e ./obj/spi-sdc.ko && echo -n yes),yes)
		sudo cp -f ./obj/spi-sdc.ko $(DRIVER_DIR_SPI)
    endif
    ifeq ($(shell test -e ./obj/sdc_mcp251x.ko && echo -n yes),yes)
		sudo cp -f ./obj/sdc_mcp251x.ko $(DRIVER_DIR_SPI_CAN)
    endif
    ifeq ($(shell test -e ./obj/sdc_mcp251xfd.ko && echo -n yes),yes)
		sudo cp -f ./obj/sdc_mcp251xfd.ko $(DRIVER_DIR_SPI_CAN)
    endif
    ifeq ($(shell test -e ./obj/can_sdc.ko && echo -n yes),yes)
		sudo cp -f ./obj/can_sdc.ko $(DRIVER_DIR_CAN)
    endif
    ifeq ($(shell test -e ./obj/gpio-sdc.ko && echo -n yes),yes)
		sudo cp -f ./obj/gpio-sdc.ko $(DRIVER_DIR_GPIO)
    endif
	sudo cp -f ../suse_installation/10-unsupported-modules.conf /etc/modprobe.d/
	sudo depmod -a

uninstall:
	@echo "Uninstall SDC driver. (kernel $(KMAJ).$(KMIN).$(KREL))"
	@echo ""
	sudo rm -f /etc/modprobe.d/10-unsupported-modules.conf
    ifeq ($(shell test -e $(DRIVER_DIR_MFD)/sunix-sdc.ko && echo -n yes),yes)
		sudo rm -f $(DRIVER_DIR_MFD)/sunix-sdc.ko
    endif
    ifeq ($(shell test -e $(DRIVER_DIR_CHAR)/sdc_config.ko && echo -n yes),yes)
		sudo rm -f $(DRIVER_DIR_CHAR)/sdc_config.ko
    endif
    ifeq ($(shell test -e $(DRIVER_DIR_8250)/8250_sdc.ko && echo -n yes),yes)
		sudo rm -f $(DRIVER_DIR_8250)/8250_sdc.ko
    endif
    ifeq ($(shell test -e $(DRIVER_DIR_CHAR)/sdc_dio.ko && echo -n yes),yes)
		sudo rm -f $(DRIVER_DIR_CHAR)/sdc_dio.ko
    endif
    ifeq ($(shell test -e $(DRIVER_DIR_SPI)/spi-sdc.ko && echo -n yes),yes)
		sudo rm -f $(DRIVER_DIR_SPI)/spi-sdc.ko
    endif
    ifeq ($(shell test -e $(DRIVER_DIR_SPI_CAN)/sdc_mcp251x.ko && echo -n yes),yes)
		sudo rm -f $(DRIVER_DIR_SPI_CAN)/sdc_mcp251x.ko
    endif
    ifeq ($(shell test -e $(DRIVER_DIR_SPI_CAN)/sdc_mcp251xfd.ko && echo -n yes),yes)
		sudo rm -f $(DRIVER_DIR_SPI_CAN)/sdc_mcp251xfd.ko
    endif
    ifeq ($(shell test -e $(DRIVER_DIR_CAN)/can_sdc.ko && echo -n yes),yes)
		sudo rm -f $(DRIVER_DIR_CAN)/can_sdc.ko
    endif
    ifeq ($(shell test -e $(DRIVER_DIR_GPIO)/gpio-sdc.ko && echo -n yes),yes)
		sudo rm -f $(DRIVER_DIR_GPIO)/gpio-sdc.ko
    endif
	sudo depmod -a
