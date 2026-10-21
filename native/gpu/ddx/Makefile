# The Mali X11 DDX (mali_drv.so), built on the box:
#   make MALI_INC=<dir with ump/ and umplock/ headers> MALI_LIB=<dir with libUMP.so>
MALI_INC ?= /usr/local/include/mali
MALI_LIB ?= /usr/local/lib/mali
MODULEDIR := $(shell pkg-config --variable=moduledir xorg-server)

CFLAGS ?= -O2 -g
# the S805's Cortex-A5 has NEON (format conversion in mali_dri.c)
CFLAGS += -mfpu=neon -fPIC -Wall -Wno-unused -include src/config.h \
	$(shell pkg-config --cflags xorg-server libdrm) -I$(MALI_INC)
LDLIBS = -L$(MALI_LIB) -Wl,-rpath,$(MALI_LIB) -lUMP -ldrm

SRC = src/mali_dri.c src/mali_exa.c src/mali_fbdev.c src/mali_lcd.c

mali_drv.so: $(SRC) src/*.h
	$(CC) $(CFLAGS) -shared -o $@ $(SRC) $(LDLIBS)

install: mali_drv.so
	install -D -m 644 mali_drv.so $(DESTDIR)$(MODULEDIR)/drivers/mali_drv.so

clean:
	rm -f mali_drv.so

.PHONY: install clean
