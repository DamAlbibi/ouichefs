obj-m += ouichefs.o
ouichefs-objs := fs.o super.o inode.o file.o dir.o

KERNELDIR ?= /media/damien/5a0d099c-9c0c-46d6-815f-8cef14f0292d/utils_kernel/linux-5.10.167

all:
	make -C $(KERNELDIR) M=$(PWD) modules

debug:
	make -C $(KERNELDIR) M=$(PWD) ccflags-y+="-DDEBUG -g" modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
	rm -rf *~

.PHONY: all clean
