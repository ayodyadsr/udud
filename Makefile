CC      ?= cc
CFLAGS  ?= -O3 -march=native -flto -Wall -Wno-misleading-indentation
LDFLAGS ?=
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man/man1

.PHONY: all install uninstall clean benchmark demo check test

all: xcull

xcull: xcull.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

runstat: runstat.c
	$(CC) -O2 -o $@ $<

benchmark: xcull runstat

install: xcull
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)
	install -m755 xcull $(DESTDIR)$(BINDIR)/xcull
	install -m644 xcull.1 $(DESTDIR)$(MANDIR)/xcull.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/xcull $(DESTDIR)$(MANDIR)/xcull.1

clean:
	rm -f xcull runstat

check: xcull
	@printf 'http://example.com/a?id=1\nhttp://example.com/a?id=2\nhttp://example.com/b\n' \
	  | ./xcull | grep -q '/b' && echo "ok: xcull built and runs"

test: xcull
	@sh tests/run.sh

demo: xcull
	@command -v vhs >/dev/null 2>&1 || { \
	  echo "vhs not found: install from https://github.com/charmbracelet/vhs"; exit 1; }
	vhs demo/demo.tape
