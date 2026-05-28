CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 \
           -Wno-unused-parameter -Wshadow -Wwrite-strings \
           -Wstrict-prototypes -Wold-style-definition \
           -Wredundant-decls -Wnested-externs -Wmissing-include-dirs
CFLAGS += -O2
LDFLAGS += $(shell pkg-config --libs x11 xft freetype2 fontconfig)
CFLAGS += $(shell pkg-config --cflags x11 xft freetype2 fontconfig)

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin

OBJS = main.o bar.o widget.o config.o
BIN = oxbar

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o: main.c bar.h widget.h config.h
bar.o: bar.c bar.h widget.h config.h
widget.o: widget.c widget.h
config.o: config.c config.h

install: all
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 config $(DESTDIR)$(HOME)/.config/oxbar/config

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all install uninstall clean
