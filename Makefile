TARGET	        := $(shell uname -r)
KERNEL_BUILD	:= /usr/src/linux-headers-$(TARGET)
DKMS_ROOT_PATH  := /usr/src/zenpower-0.1.0

obj-m	:= $(patsubst %,%.o,zenpower)
obj-ko	:= $(patsubst %,%.ko,zenpower)

.PHONY: all modules clean dkms-install dkms-uninstall

all: modules

modules:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) modules

clean:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) clean

dkms-install:
	mkdir $(DKMS_ROOT_PATH)
	cp $(CURDIR)/dkms.conf $(DKMS_ROOT_PATH)
	cp $(CURDIR)/Makefile $(DKMS_ROOT_PATH)
	cp $(CURDIR)/zenpower.c $(DKMS_ROOT_PATH)
	dkms add zenpower/0.1.0
	dkms build zenpower/0.1.0
	dkms install zenpower/0.1.0

dkms-uninstall:
	dkms remove zenpower/0.1.0 --all
	rm -rf $(DKMS_ROOT_PATH)
