CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 \
           -Wno-unused-parameter -Wshadow -Wwrite-strings \
           -Wstrict-prototypes -Wold-style-definition \
           -Wredundant-decls -Wnested-externs -Wmissing-include-dirs
CFLAGS += -O2 -Iinclude -MMD -MP
LDFLAGS += $(shell pkg-config --libs x11 xft xpm freetype2 fontconfig)
CFLAGS += $(shell pkg-config --cflags x11 xft xpm freetype2 fontconfig)

VERSION ?= 0.1.0
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

SRC = src/widget.c src/draw.c src/loop.c src/ipc.c
OBJ = $(SRC:src/%.c=build/%.o)
DEP = $(OBJ:.o=.d)

EXAMPLES = simple-bar multi-bar vertical-bar osd xmobar search

all: libox.a

examples: $(EXAMPLES)

libox.a: $(OBJ)
	$(AR) rcs $@ $^

simple-bar: examples/simple-bar.c libox.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lox $(LDFLAGS) -lm

multi-bar: examples/multi-bar.c libox.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lox $(LDFLAGS) -lm

vertical-bar: examples/vertical-bar.c libox.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lox $(LDFLAGS) -lm

osd: examples/osd.c libox.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lox $(LDFLAGS) -lm

xmobar: examples/xmobar.c libox.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lox $(LDFLAGS) -lm -lasound

search: examples/search.c libox.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lox $(LDFLAGS) -lm

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(DEP)

libox.pc: libox.pc.in
	sed -e 's|@PREFIX@|$(PREFIX)|g' \
	    -e 's|@LIBDIR@|$(LIBDIR)|g' \
	    -e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' \
	    -e 's|@VERSION@|$(VERSION)|g' \
	    -e 's|@LIBS@|-lox -lm $(shell pkg-config --libs x11 xft xpm freetype2 fontconfig)|g' \
	    -e 's|@CFLAGS@|-I$${includedir}/ox|g' \
	    $< > $@

install: libox.a libox.pc
	install -Dm644 libox.a $(DESTDIR)$(LIBDIR)/libox.a
	install -Dm644 include/ox.h $(DESTDIR)$(INCLUDEDIR)/ox/ox.h
	install -Dm644 libox.pc $(DESTDIR)$(PKGCONFIGDIR)/libox.pc

install-examples: examples
	install -Dm755 simple-bar $(DESTDIR)$(BINDIR)/ox-simple-bar
	install -Dm755 multi-bar $(DESTDIR)$(BINDIR)/ox-multi-bar
	install -Dm755 vertical-bar $(DESTDIR)$(BINDIR)/ox-vertical-bar
	install -Dm755 osd $(DESTDIR)$(BINDIR)/ox-osd
	install -Dm755 xmobar $(DESTDIR)$(BINDIR)/ox-xmobar
	install -Dm755 search $(DESTDIR)$(BINDIR)/ox-search

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/libox.a
	rm -f $(DESTDIR)$(INCLUDEDIR)/ox/ox.h
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/libox.pc
	rm -f $(DESTDIR)$(BINDIR)/ox-simple-bar
	rm -f $(DESTDIR)$(BINDIR)/ox-multi-bar
	rm -f $(DESTDIR)$(BINDIR)/ox-vertical-bar
	rm -f $(DESTDIR)$(BINDIR)/ox-osd
	rm -f $(DESTDIR)$(BINDIR)/ox-xmobar
	rm -f $(DESTDIR)$(BINDIR)/ox-search

clean:
	rm -rf build libox.a libox.pc $(EXAMPLES)

.PHONY: all examples install install-examples uninstall clean
