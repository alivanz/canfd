KDIR ?= /lib/modules/$(shell uname -r)/build
KBUILD_CFLAGS += -Wno-unused-result -Wno-missing-prototypes -Wno-missing-declarations
obj-m := xpcfd.o
all: clean module
clean:
	rm -rvf demo *.ko *.o .*.cmd *.mod.c *.mod *.order *.symvers .*tmp_versions
module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
