TARGET	        := $(shell uname -r)
DKMS_ROOT_PATH  := /usr/src/zenpower-0.1.1

ifneq ("","$(wildcard /usr/src/linux-headers-$(TARGET)/*)")
# Ubuntu
KERNEL_BUILD	:= /usr/src/linux-headers-$(TARGET)
else
ifneq ("","$(wildcard /usr/src/kernels/$(TARGET)/*)")
# Fedora
KERNEL_BUILD	:= /usr/src/kernels/$(TARGET)
else
KERNEL_BUILD	:= $(KERNEL_MODULES)/build
endif
endif

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
	dkms add zenpower/0.1.1
	dkms build zenpower/0.1.1
	dkms install zenpower/0.1.1

dkms-uninstall:
	dkms remove zenpower/0.1.1 --all
	rm -rf $(DKMS_ROOT_PATH)
