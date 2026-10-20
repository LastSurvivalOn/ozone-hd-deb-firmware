# Built on the box itself (gcc, pkg-config and the -dev packages listed in ../README.md)
CFLAGS ?= -O2 -Wall -Wextra
PKGS = libavformat libavcodec libswresample libavutil alsa x11 xft

amplay: amplay.c osd.c osd.h
	$(CC) $(CFLAGS) -o $@ amplay.c osd.c $$(pkg-config --cflags --libs $(PKGS)) -lpthread

install: amplay
	install -D -m 755 amplay $(DESTDIR)/usr/local/bin/amplay

clean:
	rm -f amplay

.PHONY: install clean
