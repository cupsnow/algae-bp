# $Id$
# 
# Copyright (c) 2025, joelai
# All Rights Reserved
# 
# SPDX-License-Identifier: MIT
# 
# @file noname
# @brief noname

#------------------------------------
#
PROJDIR?=$(PWD)
include $(PROJDIR)/builder/proj.mk

CPPFLAGS+=-g
CFLAGS+=
CXXFLAGS+=
LDFLAGS+=

GENDIR:=

ARTIFACT:=
ARTIFACT_INSTALL:=

.DEFAULT_GOAL:=artifact

#------------------------------------
#
aloe_BUILDDIR=$(BUILDDIR)/aloe$(or $(APP_BUILD:%=-%),$(APP_PLATFORM:%=-%))
aloe_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/,*.cpp *.c))
aloe_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/sys_linux/,*.cpp *.c))
aloe_OBJS=$(patsubst $(PROJDIR)/package/aloe/%,$(aloe_BUILDDIR)/%.o,$(filter %.c %.cpp,$(aloe_SRCS)))
aloe_DEPS=$(aloe_OBJS:%=%.d)
aloe_GENDIR=$(sort $(dir $(aloe_OBJS)))
aloe_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX

GENDIR+=$(aloe_GENDIR)

ARTIFACT+=aloe

.PHONY: aloe
aloe: $(aloe_OBJS)

$(filter %.c.o,$(aloe_OBJS)): $(aloe_BUILDDIR)/%.o : $(PROJDIR)/package/aloe/% | $(aloe_GENDIR)
	$(CC) -c -o $@ $< $(DEPFLAGS) $(or $(aloe_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(aloe_CFLAGS),$(CFLAGS))

$(filter %.cpp.o,$(aloe_OBJS)): $(aloe_BUILDDIR)/%.o : $(PROJDIR)/package/aloe/% | $(aloe_GENDIR)
	$(C++) -c -o $@ $< $(DEPFLAGS) $(or $(aloe_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(aloe_CXXFLAGS),$(CXXFLAGS))

#------------------------------------
#
define APP1_BUILD
$(1)_BUILDDIR?=$$(BUILDDIR)/$(1)$$(or $$(APP_BUILD:%=-%),$$(APP_PLATFORM:%=-%))
$(1)_BIN?=$(1)
$(1)_SRCS+=$(2)
$(1)_OBJS+=$$(patsubst %,$$($(1)_BUILDDIR)/%.o,$$(filter %.c %.cpp,$$($(1)_SRCS)))
$(1)_DEPS+=$$($(1)_OBJS:%.o=%.o.d)
$(1)_CPPFLAGS+=$$(CPPFLAGS)
$(1)_GENDIR+=$$(sort $$(dir $$($(1)_OBJS)))

GENDIR+=$$($(1)_GENDIR)

.PHONY: $(1)
$(1): $$($(1)_BUILDDIR)/$$($(1)_BIN)
 
$$($(1)_BUILDDIR)/$$($(1)_BIN): $$($(1)_OBJS) $$($(1)_SRCS)
	$$(C++) -o $$($(1)_BUILDDIR)/$$($(1)_BIN) $$(filter %.o %.a,$$^) \
	    $$(or $$($(1)_LDFLAGS),$$(LDFLAGS))

$$(filter %.c.o,$$($(1)_OBJS)): $$($(1)_BUILDDIR)/%.o : % | $$($(1)_GENDIR)
	$$(CC) -c -o $$@ $$< $$(DEPFLAGS) $$(or $$($(1)_CPPFLAGS),$$(CPPFLAGS)) \
	    $$(or $$($(1)_CFLAGS),$$(CFLAGS))

$$(filter %.cpp.o,$$($(1)_OBJS)): $$($(1)_BUILDDIR)/%.o : % | $$($(1)_GENDIR)
	$$(C++) -c -o $$@ $$< $$(DEPFLAGS) $$(or $$($(1)_CPPFLAGS),$$(CPPFLAGS)) \
	    $$(or $$($(1)_CXXFLAGS),$$(CXXFLAGS))
# end of APP1_BUILD
endef

#------------------------------------
#

tester1_CPPFLAGS+=$(aloe_CPPFLAGS)

$(eval $(call APP1_BUILD,tester1, \
  main.cpp mgmt.cpp cli.cpp ipc.cpp $(aloe_OBJS) \
))

ARTIFACT+=tester1

#------------------------------------
#
th1_CPPFLAGS+=$(aloe_CPPFLAGS)

$(eval $(call APP1_BUILD,th1, \
  th1.cpp $(aloe_OBJS) \
))

ARTIFACT+=th1

#------------------------------------
#
th2_CPPFLAGS+=$(aloe_CPPFLAGS)

$(eval $(call APP1_BUILD,th2, \
  th2.cpp $(aloe_OBJS) \
))

ARTIFACT+=th2

#------------------------------------
#
doxygen: OUTPUT_DIRECTORY=$(BUILDDIR)/aloe-docs
doxygen:
	[ -d "$(OUTPUT_DIRECTORY)" ] || $(MKDIR) $(OUTPUT_DIRECTORY)
	cd $(PROJDIR)/package/aloe && \
	  { cat Doxyfile && \
	  echo "PROJECT_NUMBER=0.0.1" && \
	  echo "INPUT=include" &&  \
	  echo "OUTPUT_DIRECTORY=$(OUTPUT_DIRECTORY)" && \
	  echo ""; } | doxygen - >$(tester1_BUILDDIR)/doxygen.log 2>&1

#------------------------------------
#
fb1_BUILDDIR=$(BUILDDIR)/tester1$(or $(APP_BUILD:%=-%),$(APP_PLATFORM:%=-%))
fb1_BIN=tester_fb
fb1_SRCS=tester_fb1.c
fb1_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/,*.cpp *.c))
fb1_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/sys_linux/,*.cpp *.c))
# fb1_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe_ev/,*.cpp *.c))
fb1_OBJS=$(patsubst %,$(fb1_BUILDDIR)/%.o,$(filter %.c %.cpp,$(fb1_SRCS)))
fb1_DEPS=$(fb1_OBJS:%=%.d)
#fb1_LDFLAGS=-static $(LDFLAGS)
fb1_CPPFLAGS+=$(CPPFLAGS)
fb1_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX=1

ARTIFACT+=fb1

GENDIR+=$(fb1_BUILDDIR)
 
fb1: $(fb1_BUILDDIR)/$(fb1_BIN)

$(fb1_BUILDDIR)/$(fb1_BIN): $(fb1_OBJS)
	$(C++) -o $(fb1_BUILDDIR)/$(fb1_BIN) $(filter %.o %.a,$^) \
	    $(or $(fb1_LDFLAGS),$(LDFLAGS))

$(fb1_BUILDDIR)/%.c.o: %.c
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(CC) -c -o $@ $< $(DEPFLAGS) $(or $(fb1_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(fb1_CFLAGS),$(CFLAGS))

$(fb1_BUILDDIR)/%.cpp.o: %.cpp
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(C++) -c -o $@ $< $(DEPFLAGS) $(or $(fb1_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(fb1_CXXFLAGS),$(CXXFLAGS))

#------------------------------------
#
artifact: $(ARTIFACT) doxygen

-include $(sort $(foreach iter,$(ARTIFACT),$(${iter}_DEPS)))

#------------------------------------
#
$(sort $(GENDIR)):
	$(MKDIR) $@

#------------------------------------
#------------------------------------
#------------------------------------
#
