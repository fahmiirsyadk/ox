CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 \
           -Wno-unused-parameter -Wshadow -Wwrite-strings \
           -Wstrict-prototypes -Wold-style-definition \
           -Wredundant-decls -Wnested-externs -Wmissing-include-dirs
CFLAGS += -O2 -Iinclude -MMD -MP
LDFLAGS += $(shell pkg-config --libs x11 xft xpm freetype2 fontconfig)
CFLAGS += $(shell pkg-config --cflags x11 xft xpm freetype2 fontconfig)

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin

SRC = src/widget.c src/draw.c src/loop.c src/ipc.c
OBJ = $(SRC:src/%.c=build/%.o)
DEP = $(OBJ:.o=.d)

EXAMPLES = simple-bar multi-bar vertical-bar osd xmobar search

all: $(EXAMPLES)

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
	$(CC) $(CFLAGS) -o $@ $< -L. -lox $(LDFLAGS) -lm

search: examples/search.c libox.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lox $(LDFLAGS) -lm

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(DEP)

install: all
	install -Dm755 simple-bar $(DESTDIR)$(BINDIR)/ox-simple-bar
	install -Dm755 multi-bar $(DESTDIR)$(BINDIR)/ox-multi-bar
	install -Dm755 vertical-bar $(DESTDIR)$(BINDIR)/ox-vertical-bar
	install -Dm755 osd $(DESTDIR)$(BINDIR)/ox-osd
	install -Dm755 xmobar $(DESTDIR)$(BINDIR)/ox-xmobar

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ox-simple-bar
	rm -f $(DESTDIR)$(BINDIR)/ox-multi-bar
	rm -f $(DESTDIR)$(BINDIR)/ox-vertical-bar
	rm -f $(DESTDIR)$(BINDIR)/ox-osd
	rm -f $(DESTDIR)$(BINDIR)/ox-xmobar

clean:
	rm -rf build libox.a $(EXAMPLES)

.PHONY: all install uninstall clean
