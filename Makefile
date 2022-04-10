obj-m += tarfs.o
tarfs-y := proc.o super.o inode.o namei.o dir.o file.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

default:
	make -C $(KERNELDIR) M=$(PWD) modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
