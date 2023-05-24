obj-m += ouichefs.o
ouichefs-objs := fs.o super.o inode.o file.o dir.o

KERNELDIR ?= /home/drxeno/programmation/PNL/projet_ouichefs/linux-5.10.167

all:
	make -C $(KERNELDIR) M=$(PWD) modules

debug:
	make -C $(KERNELDIR) M=$(PWD) ccflags-y+="-DDEBUG -g" modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
	rm -rf *~

.PHONY: all clean
