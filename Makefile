.POSIX:

CC      ?= cc
CXX     ?= c++
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man

PKGS     = sdl3 md4c freetype2 harfbuzz fontconfig source-highlight
CPPFLAGS = -D_POSIX_C_SOURCE=200809L $(shell pkg-config --cflags $(PKGS))
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Wshadow
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++11 -Wall -Wextra -Wpedantic -Wshadow
LDFLAGS ?=
LDLIBS   = $(shell pkg-config --libs $(PKGS)) -lm

SRC = \
	src/main.c \
	src/arena.c \
	src/file.c \
	src/theme.c \
	src/flavor.c \
	src/document.c \
	src/markdown.c \
	src/font.c \
	src/layout_inline.c \
	src/layout.c \
	src/selection.c \
	src/render.c
CXXSRC = src/highlight.cc
OBJ = $(SRC:.c=.o) $(CXXSRC:.cc=.o)

all: mdwn

mdwn: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

.cc.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O0 -g3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined' mdwn check

install: mdwn
	mkdir -p '$(DESTDIR)$(BINDIR)' '$(DESTDIR)$(MANDIR)/man1'
	install -m 0755 mdwn '$(DESTDIR)$(BINDIR)/mdwn'
	install -m 0644 mdwn.1 '$(DESTDIR)$(MANDIR)/man1/mdwn.1'

uninstall:
	rm -f '$(DESTDIR)$(BINDIR)/mdwn' '$(DESTDIR)$(MANDIR)/man1/mdwn.1'

clean:
	rm -f mdwn $(OBJ)

.PHONY: all check sanitize install uninstall clean
