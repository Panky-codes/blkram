MY_CFLAGS += -g
ccflags-y += ${MY_CFLAGS}

obj-m += blkram.o


all:
ifeq ($(kdir),)
	@echo Please set the kernel dir parameter as follows:
	@echo make kdir="/path/to/kernel"
	@echo
	@exit 1
endif
	$(MAKE) -C $(kdir) M=$$PWD EXTRA_CFLAGS="$(MY_CFLAGS)"

.PHONY: clean
clean:
	rm -f blkram.ko  blkram.mod  blkram.mod.c  blkram.mod.o \
		blkram.o modules.order  Module.symvers
