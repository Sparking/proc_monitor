-include $(KCONFIG)

mod_name         := proc_monitor
obj-m            := $(mod_name).o
$(mod_name)-objs := entry.o proc_fs.o history.o hash_table.o
$(mod_name)-objs += monitor_percpu.o monitor_memory.o monitor_task.o

EXPORT_HEADER    := $(PRJROOT)/images/header/rg-sysmon
MOD_INSTALL_PATH := $(ROOTFS_IMAGES)/lib/modules

ifeq ("$(KBUILD_OUTPUT)", "")
LINUX_KERNEL_PATH := /usr/src/linux-headers-$(shell uname -r)
KBUILD_OUTPUT := $(LINUX_KERNEL_PATH)
ROOTFS_IMAGES := $(CURDIR)/rootfs
.PHONY: all
all: build
endif

.PHONY: install
install: build
	install -m0755 -d $(EXPORT_HEADER)
	install -m0755 -d $(MOD_INSTALL_PATH)
	install -m0644 -t $(EXPORT_HEADER) monitor_path.h
	install -m0644 -t $(MOD_INSTALL_PATH) $(mod_name).ko
ifeq ($(SYSMON_PLATFORM_EU),)
	install -m0755 -d $(ROOTFS_IMAGES)/lib/systemd/system
	install -m0755 -d $(ROOTFS_IMAGES)/etc/systemd/system/multi-user.target.wants
	install -m0644 -t $(ROOTFS_IMAGES)/lib/systemd/system scripts/$(mod_name).service
	ln -sf /lib/systemd/system/$(mod_name).service \
	    $(ROOTFS_IMAGES)/etc/systemd/system/multi-user.target.wants/$(mod_name).service
else
	install -m0755 -d $(ROOTFS_IMAGES)/etc/rc.d/rc3.d
	install -m0755 -t $(ROOTFS_IMAGES)/etc/rc.d/rc3.d scripts/S01-proc_monitor
endif

.PHONY: build
build:
	$(MAKE) -C $(KBUILD_OUTPUT) M=$(CURDIR) clean
	$(MAKE) -C $(KBUILD_OUTPUT) M=$(CURDIR) modules

.PHONY: insmod rmmod
insmod:
	sudo insmod $(mod_name).ko process_hash_size=289 thread_hash_size=560
rmmod:
	sudo rmmod $(mod_name)
