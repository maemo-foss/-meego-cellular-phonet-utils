#! /usr/bin/make -f

PACKAGE = phonet-utils

CFLAGS ?= -g -O2 -Wall -Wextra
CC ?= $(CROSS_COMPILE)gcc
override CC += -std=gnu99
INSTALL ?= install

#prefix ?= /usr/local
prefix ?=
libdir ?= $(prefix)/lib
pkglibdir ?= $(libdir)/$(PACKAGE)
sbindir ?= $(prefix)/sbin
sysconfdir ?= $(prefix)/etc

TARGETS := phonet pnroute pnstat

all: $(TARGETS)

clean: 
	rm -f -- $(TARGETS) *.o *~

install: all
	for f in $(TARGETS); do \
       		$(INSTALL) -D -m 0755 "$$f" \
			"$(DESTDIR)$(sbindir)/$$f" || exit $$? ; \
	done
	install -D -m 0755 ../misc/if-pre-up \
		"$(DESTDIR)$(pkglibdir)/if-pre-up"
	install -D -m 0755 ../misc/phonet-utils.rules \
		"$(DESTDIR)/lib/udev/rules.d/85-phonet-utils.rules"
