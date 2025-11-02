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
tester1_BUILDDIR=$(BUILDDIR)/tester1$(or $(APP_BUILD:%=-%),$(APP_PLATFORM:%=-%))
tester1_BIN=tester1
tester1_SRCS=main.cpp
tester1_SRCS+=mgmt.cpp cli.cpp ipc.cpp
tester1_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/,*.cpp *.c))
tester1_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/sys_linux/,*.cpp *.c))
# tester1_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe_ev/,*.cpp *.c))
tester1_OBJS=$(patsubst %,$(tester1_BUILDDIR)/%.o,$(filter %.c %.cpp,$(tester1_SRCS)))
tester1_DEPS=$(tester1_OBJS:%=%.d)
#tester1_LDFLAGS=-static $(LDFLAGS)
tester1_CPPFLAGS+=$(CPPFLAGS)
tester1_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX=1

ARTIFACT+=tester1

GENDIR+=$(tester1_BUILDDIR)
 
tester1: $(tester1_BUILDDIR)/$(tester1_BIN)

$(tester1_BUILDDIR)/$(tester1_BIN): $(tester1_OBJS)
	$(C++) -o $(tester1_BUILDDIR)/$(tester1_BIN) $(filter %.o %.a,$^) \
	    $(or $(tester1_LDFLAGS),$(LDFLAGS))

$(tester1_BUILDDIR)/%.c.o: %.c
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(CC) -c -o $@ $< $(DEPFLAGS) $(or $(tester1_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(tester1_CFLAGS),$(CFLAGS))

$(tester1_BUILDDIR)/%.cpp.o: %.cpp
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(C++) -c -o $@ $< $(DEPFLAGS) $(or $(tester1_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(tester1_CXXFLAGS),$(CXXFLAGS))

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
