mod_name := read_proc
obj-m    := $(mod_name).o
$(mod_name)-objs := entry.o proc_fs.o history.o percpu_monitor.o task_monitor.o

ifeq ($(EXPORT_HEADER),)
EXPORT_HEADER  := $(PRJROOT)/images/header
endif
EXTRA_CFLAGS   += -I$(EXPORT_HEADER)

ifeq ("$(KBUILD_OUTPUT)", "")
LINUX_KERNEL_PATH := /usr/src/linux-headers-$(shell uname -r)
KBUILD_OUTPUT := $(LINUX_KERNEL_PATH)
ROOTFS_IMAGES := $(CURDIR)/rootfs
endif

MOD_INSTALL_PATH := $(ROOTFS_IMAGES)/lib/modules

.PHONY: install
install: build
	install -m0755 -d $(MOD_INSTALL_PATH)
	install -m0755 -t $(MOD_INSTALL_PATH) $(mod_name).ko
	install -m0755 -d $(ROOTFS_IMAGES)/etc/systemd/system/multi-user.target.wants
	install -m0644 -t $(ROOTFS_IMAGES)/lib/systemd/system read_proc.service
	@rm -f $(ROOTFS_IMAGES)/etc/systemd/system/multi-user.target.wants/read_proc.service
	ln -s /lib/systemd/system/read_proc.service $(ROOTFS_IMAGES)/etc/systemd/system/multi-user.target.wants/read_proc.service

.PHONY: build
build: clean
	$(MAKE) -C $(KBUILD_OUTPUT) M=$(CURDIR) modules

.PHONY: clean
clean:
	$(MAKE) -C $(KBUILD_OUTPUT) M=$(CURDIR) clean

.PHONY: insmod
insmod:
	@sudo insmod $(MOD_INSTALL_PATH)/$(mod_name).ko

.PHONY: rmmod
rmmod:
	@sudo rmmod $(mod_name)
