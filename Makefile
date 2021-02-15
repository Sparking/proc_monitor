mod_name := proc_monitor
obj-m    := $(mod_name).o
$(mod_name)-objs := entry.o proc_fs.o history.o hash_table.o
$(mod_name)-objs += monitor_percpu.o monitor_memory.o monitor_task.o

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
	install -m0755 -d $(ROOTFS_IMAGES)/lib/systemd/system
	install -m0644 -t $(ROOTFS_IMAGES)/lib/systemd/system read_proc.service
	install -m0755 -d $(ROOTFS_IMAGES)/etc/systemd/system/multi-user.target.wants
	@rm -f $(ROOTFS_IMAGES)/etc/systemd/system/multi-user.target.wants/read_proc.service
	ln -s /lib/systemd/system/read_proc.service \
	    $(ROOTFS_IMAGES)/etc/systemd/system/multi-user.target.wants/read_proc.service

.PHONY: build
build: clean
	$(MAKE) -j4 -C $(KBUILD_OUTPUT) M=$(CURDIR) modules

.PHONY: clean
clean:
	$(MAKE) -j4 -C $(KBUILD_OUTPUT) M=$(CURDIR) clean

.PHONY: insmod
insmod:
	@sudo insmod $(MOD_INSTALL_PATH)/$(mod_name).ko process_hash_size=128 thread_hash_size=128

.PHONY: rmmod
rmmod:
	@sudo rmmod $(mod_name)
