.POSIX:

CC      ?= cc
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man

PKGS     = sdl3 md4c freetype2 harfbuzz fontconfig
CPPFLAGS = -D_POSIX_C_SOURCE=200809L $(shell pkg-config --cflags $(PKGS))
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Wshadow
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
	src/layout.c \
	src/render.c
OBJ = $(SRC:.c=.o)

TEST_SRC = test/markdown.c src/arena.c src/theme.c src/flavor.c src/document.c src/markdown.c
TEST_BIN = test/markdown-test

all: mdwn

mdwn: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(TEST_BIN): $(TEST_SRC)
	$(CC) -D_POSIX_C_SOURCE=200809L -Isrc \
		$(shell pkg-config --cflags md4c) $(CFLAGS) \
		-o $@ $(TEST_SRC) $(shell pkg-config --libs md4c)

check: $(TEST_BIN)
	./$(TEST_BIN)

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
	rm -f mdwn $(OBJ) $(TEST_BIN)

.PHONY: all check sanitize install uninstall clean
