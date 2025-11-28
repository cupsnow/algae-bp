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
dummy1_BUILDDIR=$(BUILDDIR)/dummy1$(or $(APP_BUILD:%=-%),$(APP_PLATFORM:%=-%))
dummy1_BIN=dummy1
dummy1_SRCS=main.cpp
dummy1_SRCS+=mgmt.cpp cli.cpp ipc.cpp
dummy1_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/,*.cpp *.c))
dummy1_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/sys_linux/,*.cpp *.c))
# dummy1_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe_ev/,*.cpp *.c))
dummy1_OBJS=$(patsubst %,$(dummy1_BUILDDIR)/%.o,$(filter %.c %.cpp,$(dummy1_SRCS)))
dummy1_DEPS=$(dummy1_OBJS:%=%.d)
#dummy1_LDFLAGS+=
dummy1_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX=1

GENDIR+=$(dummy1_BUILDDIR) $(dir $(dummy1_OBJS)) $(dir $(dummy1_BUILDDIR)/$(dummy1_BIN))

.PHONY: dummy1
dummy1: $(dummy1_BUILDDIR)/$(dummy1_BIN)

$(dummy1_BUILDDIR)/$(dummy1_BIN): $(dummy1_OBJS)
	$(if $(filter %.cpp,$(dummy1_SRCS)),$(C++),$(CC)) \
	    -o $@ -Wl,--start-group \
	    $(filter %.o %.a,$^ $(dummy1_SRCS)) \
	    -Wl,--end-group \
	    -pthread $(LDFLAGS) $(dummy1_LDFLAGS)
	$(OBJDUMP) -xdt $@ \
	    >$(@).lst

$(dummy1_BUILDDIR)/%.c.o: %.c
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(CC) -c -o $@ $< $(DEPFLAGS) \
	    -pthread $(CPPFLAGS) $(dummy1_CPPFLAGS) \
	    $(CFLAGS) $(dummy1_CFLAGS)

$(dummy1_BUILDDIR)/%.cpp.o: %.cpp
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(C++) -c -o $@ $< $(DEPFLAGS) \
	    -pthread $(CPPFLAGS) $(dummy1_CPPFLAGS) \
	    $(CXXFLAGS) $(dummy1_CXXFLAGS)

# -include $(dummy1_DEPS)

dummy1_docs: OUTPUT_DIRECTORY=$(dummy1_BUILDDIR)/docs
dummy1_docs:
	[ -d "$(OUTPUT_DIRECTORY)" ] || $(MKDIR) $(OUTPUT_DIRECTORY)
	{ cat $(PROJDIR)/package/aloe/Doxyfile && \
	  echo "PROJECT_NUMBER=0.0.1" && \
	  echo "INPUT=$(PROJDIR)/package/aloe/include $(abspath .)" &&  \
	  echo "OUTPUT_DIRECTORY=$(OUTPUT_DIRECTORY)" && \
	  echo ""; } | doxygen - >$(dummy1_BUILDDIR)/doxygen.log 2>&1

ARTIFACT+=dummy1
ARTIFACT_DOCS+=dummy1_docs

#------------------------------------
#
artifact: $(ARTIFACT) $(ARTIFACT_DOCS)

-include $(sort $(foreach iter,$(ARTIFACT),$(${iter}_DEPS)))

#------------------------------------
#
$(sort $(GENDIR)):
	$(MKDIR) $@

#------------------------------------
#------------------------------------
#------------------------------------
#
