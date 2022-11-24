MY_CFLAGS += -g
ccflags-y += ${MY_CFLAGS}

obj-m += blkram.o

all:
	$(MAKE) -C $(kdir) M=$$PWD EXTRA_CFLAGS="$(MY_CFLAGS)"
	scp blkram.ko vmctl:

clean:
	$(MAKE) -C $(kdir) M=$$PWD clean
