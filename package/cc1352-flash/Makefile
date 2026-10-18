#------------------------------------
#
CC?=$(CROSS_COMPILE)gcc
CFLAGS+=-std=gnu11 -Wall -Wextra -Wpedantic
CFLAGS+=-g
LDFLAGS+=

SRCDIR?=$(CURDIR)
O?=$(SRCDIR)/build
cc1352flash_BIN?=cc1352-flash

.DEFAULT_GOAL:=all

.PHONY: all
all: $(O)/$(cc1352flash_BIN)

install: $(O)/$(cc1352flash_BIN)
	install -Dm755 $(O)/$(cc1352flash_BIN) $(DESTDIR)$(PREFIX)/sbin/$(cc1352flash_BIN)

clean:
	rm -rf $(O)/$(cc1352flash_BIN)
	rmdir --ignore-fail-on-non-empty $(O)

$(O)/$(cc1352flash_BIN): GENDIR+=$(O)
$(O)/$(cc1352flash_BIN): $(SRCDIR)/cc1352-flash.c | $(O)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(sort $(GENDIR)):
	mkdir -p $@
