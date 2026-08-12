CC      ?= cc
CXX     ?= c++
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man

BUILDDIR     = build
OBJDIR       = $(BUILDDIR)/obj
BUILD_BINDIR = $(BUILDDIR)/bin
PROGRAM      = $(BUILD_BINDIR)/mdwn

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

PKGS     = sdl3 sdl3-ttf md4c fontconfig source-highlight tomlplusplus
CPPFLAGS = -D_POSIX_C_SOURCE=200809L $(shell pkg-config --cflags $(PKGS))
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Wshadow
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Wshadow
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
CXXSRC = src/highlight.cc src/config.cc
OBJ = $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRC)) \
	$(patsubst src/%.cc,$(OBJDIR)/%.o,$(CXXSRC))
VERSION_HEADER = src/version.h
MANPAGE = mdwn.1

all: $(PROGRAM) $(MANPAGE)

mdwn: $(PROGRAM)

$(PROGRAM): $(OBJ)
	mkdir -p '$(BUILD_BINDIR)'
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

$(OBJDIR)/main.o: $(VERSION_HEADER)

$(MANPAGE): mdwn.1.in FORCE VERSION
	@sed 's/@VERSION@/$(VERSION)/g' mdwn.1.in > '$@.tmp'
	@if ! test -r '$@' || ! cmp -s '$@.tmp' '$@'; then \
		mv '$@.tmp' '$@'; \
	else \
		rm -f '$@.tmp'; \
	fi

$(OBJDIR)/%.o: src/%.c
	mkdir -p '$(OBJDIR)'
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: src/%.cc
	mkdir -p '$(OBJDIR)'
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O0 -g3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined' mdwn

install:
	@if ! $(MAKE) --no-print-directory -q -o FORCE all; then \
		printf '%s\n' 'Build is missing or outdated; run make before make install.' >&2; \
		exit 1; \
	fi
	mkdir -p '$(DESTDIR)$(BINDIR)' '$(DESTDIR)$(MANDIR)/man1'
	install -m 0755 '$(PROGRAM)' '$(DESTDIR)$(BINDIR)/mdwn'
	install -m 0644 '$(MANPAGE)' '$(DESTDIR)$(MANDIR)/man1/mdwn.1'

uninstall:
	rm -f '$(DESTDIR)$(BINDIR)/mdwn' '$(DESTDIR)$(MANDIR)/man1/mdwn.1'

clean:
	rm -f $(PROGRAM) $(OBJ) $(VERSION_HEADER) $(MANPAGE)
	rmdir '$(OBJDIR)' '$(BUILD_BINDIR)' '$(BUILDDIR)' 2>/dev/null || :

FORCE:

.PHONY: all mdwn sanitize install uninstall clean FORCE
