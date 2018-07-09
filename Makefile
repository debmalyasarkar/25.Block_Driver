#Common Makefile

#Target and Input File
obj-m	:= ramdisk_blkdrv.o

#Variable/Macro to hold kernel-headers or kernel source directory path
KDIR = /lib/modules/$(shell uname -r)/build

#Commands to convert input files into desired target
#These commands uses KBuild Scripts found in kernel-headers/kernel source path
#-C Option Changes Directory to kernel source.
# M Argument Instructs Make to Switch Back to Module Directory.
# SUBDIR is valid for static modules as well as dynamic modules.(Legacy Option)
#Build Targets
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

#Copy Module Signature to the In-Tree SymVers
install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

#Delete Targets
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

