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
dummy_BUILDDIR=$(BUILDDIR)/dummy1$(or $(APP_BUILD:%=-%),$(APP_PLATFORM:%=-%))
dummy_BIN=dummy1
dummy_SRCS=main.cpp
dummy_SRCS+=mgmt.cpp cli.cpp ipc.cpp
dummy_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/,*.cpp *.c))
dummy_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/sys_linux/,*.cpp *.c))
# dummy_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe_ev/,*.cpp *.c))
dummy_OBJS=$(patsubst %,$(dummy_BUILDDIR)/%.o,$(filter %.c %.cpp,$(dummy_SRCS)))
dummy_DEPS=$(dummy_OBJS:%=%.d)
#dummy_LDFLAGS=-static $(LDFLAGS)
dummy_CPPFLAGS+=$(CPPFLAGS)
dummy_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX=1

ARTIFACT+=dummy

GENDIR+=$(dummy_BUILDDIR)
 
dummy: $(dummy_BUILDDIR)/$(dummy_BIN)

$(dummy_BUILDDIR)/$(dummy_BIN): $(dummy_OBJS)
	$(C++) -o $(dummy_BUILDDIR)/$(dummy_BIN) $(filter %.o %.a,$^) \
	    $(or $(dummy_LDFLAGS),$(LDFLAGS))

$(dummy_BUILDDIR)/%.c.o: %.c
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(CC) -c -o $@ $< $(DEPFLAGS) $(or $(dummy_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(dummy_CFLAGS),$(CFLAGS))

$(dummy_BUILDDIR)/%.cpp.o: %.cpp
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(C++) -c -o $@ $< $(DEPFLAGS) $(or $(dummy_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(dummy_CXXFLAGS),$(CXXFLAGS))

doxygen: OUTPUT_DIRECTORY=$(BUILDDIR)/aloe-docs
doxygen:
	[ -d "$(OUTPUT_DIRECTORY)" ] || $(MKDIR) $(OUTPUT_DIRECTORY)
	cd $(PROJDIR)/package/aloe && \
	  { cat Doxyfile && \
	  echo "PROJECT_NUMBER=0.0.1" && \
	  echo "INPUT=include" &&  \
	  echo "OUTPUT_DIRECTORY=$(OUTPUT_DIRECTORY)" && \
	  echo ""; } | doxygen - >$(dummy_BUILDDIR)/doxygen.log 2>&1

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
