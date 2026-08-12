.POSIX:

CC      ?= cc
CXX     ?= c++
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man

BASE_VERSION = $(shell sed -n '1p' VERSION)
GIT_TAG      = $(shell git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null)
GIT_REV      = $(shell git rev-parse --short HEAD 2>/dev/null)
GIT_DIRTY    = $(shell git diff-index --quiet HEAD -- 2>/dev/null || printf '.dirty')

ifneq ($(GIT_TAG),)
VERSION = $(patsubst v%,%,$(GIT_TAG))$(if $(GIT_DIRTY),+dirty)
else ifneq ($(GIT_REV),)
VERSION = $(BASE_VERSION)-dev+$(GIT_REV)$(GIT_DIRTY)
else
VERSION = $(BASE_VERSION)-dev
endif

PKGS     = sdl3 sdl3-ttf md4c fontconfig source-highlight
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
VERSION_HEADER = src/version.h
MANPAGE = mdwn.1

all: mdwn $(MANPAGE)

mdwn: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(VERSION_HEADER): FORCE VERSION
	@printf '%s\n' \
		'#ifndef MDWN_VERSION_H' \
		'#define MDWN_VERSION_H' \
		'#define MDWN_VERSION "$(VERSION)"' \
		'#endif' > '$@.tmp'
	@if ! test -r '$@' || ! cmp -s '$@.tmp' '$@'; then \
		mv '$@.tmp' '$@'; \
	else \
		rm -f '$@.tmp'; \
	fi

src/main.o: $(VERSION_HEADER)

$(MANPAGE): mdwn.1.in FORCE VERSION
	@sed 's/@VERSION@/$(VERSION)/g' mdwn.1.in > '$@.tmp'
	@if ! test -r '$@' || ! cmp -s '$@.tmp' '$@'; then \
		mv '$@.tmp' '$@'; \
	else \
		rm -f '$@.tmp'; \
	fi

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

.cc.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O0 -g3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined' mdwn

install: mdwn $(MANPAGE)
	mkdir -p '$(DESTDIR)$(BINDIR)' '$(DESTDIR)$(MANDIR)/man1'
	install -m 0755 mdwn '$(DESTDIR)$(BINDIR)/mdwn'
	install -m 0644 mdwn.1 '$(DESTDIR)$(MANDIR)/man1/mdwn.1'

uninstall:
	rm -f '$(DESTDIR)$(BINDIR)/mdwn' '$(DESTDIR)$(MANDIR)/man1/mdwn.1'

clean:
	rm -f mdwn $(OBJ) $(VERSION_HEADER) $(MANPAGE)

FORCE:

.PHONY: all sanitize install uninstall clean FORCE
