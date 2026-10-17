CC ?= cc
CFLAGS ?= -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wpedantic

SRCDIR ?= $(CURDIR)
O ?= $(SRCDIR)

all: cc1352-flash
.PHONY: all

$(O)/cc1352-flash: $(SRCDIR)/cc1352-flash.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

.PHONY: cc1352-flash
cc1352-flash: $(O)/cc1352-flash


