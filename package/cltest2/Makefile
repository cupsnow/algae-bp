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
cltest2_BUILDDIR=$(BUILDDIR)/cltest2$(or $(APP_BUILD:%=-%),$(APP_PLATFORM:%=-%))
cltest2_BIN=cltest2
cltest2_SRCS=cltest2.cpp
cltest2_SRCS+=
cltest2_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/,*.cpp *.c))
cltest2_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe/sys_linux/,*.cpp *.c))
# cltest2_SRCS+=$(wildcard $(addprefix $(PROJDIR)/package/aloe_ev/,*.cpp *.c))
cltest2_OBJS=$(patsubst %,$(cltest2_BUILDDIR)/%.o,$(filter %.c %.cpp,$(cltest2_SRCS)))
cltest2_DEPS=$(cltest2_OBJS:%=%.d)
cltest2_LDFLAGS+=-lOpenCL
cltest2_CPPFLAGS+=-g
cltest2_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX=1

GENDIR+=$(cltest2_BUILDDIR) $(dir $(cltest2_OBJS)) $(dir $(cltest2_BUILDDIR)/$(cltest2_BIN))

.PHONY: cltest2
cltest2: $(cltest2_BUILDDIR)/$(cltest2_BIN)

$(cltest2_BUILDDIR)/$(cltest2_BIN): $(cltest2_OBJS)
	$(if $(filter %.cpp,$(cltest2_SRCS)),$(C++),$(CC)) \
	    -o $@ -Wl,--start-group \
	    $(filter %.o %.a,$^ $(cltest2_SRCS)) \
	    -Wl,--end-group \
	    -pthread $(LDFLAGS) $(cltest2_LDFLAGS)
	$(OBJDUMP) -xdt $@ \
	    >$(@).lst

$(cltest2_BUILDDIR)/%.c.o: %.c
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(CC) -c -o $@ $< $(DEPFLAGS) \
	    -pthread $(CPPFLAGS) $(cltest2_CPPFLAGS) \
	    $(CFLAGS) $(cltest2_CFLAGS)

$(cltest2_BUILDDIR)/%.cpp.o: %.cpp
	[ -d "$(@D)" ] || $(MKDIR) $(@D)
	$(C++) -c -o $@ $< $(DEPFLAGS) \
	    -pthread $(CPPFLAGS) $(cltest2_CPPFLAGS) \
	    $(CXXFLAGS) $(cltest2_CXXFLAGS)

# -include $(cltest2_DEPS)

cltest2_docs: OUTPUT_DIRECTORY=$(cltest2_BUILDDIR)/docs
cltest2_docs:
	[ -d "$(OUTPUT_DIRECTORY)" ] || $(MKDIR) $(OUTPUT_DIRECTORY)
	{ cat $(PROJDIR)/package/aloe/Doxyfile && \
	  echo "PROJECT_NUMBER=0.0.1" && \
	  echo "INPUT=$(PROJDIR)/package/aloe/include $(abspath .)" &&  \
	  echo "OUTPUT_DIRECTORY=$(OUTPUT_DIRECTORY)" && \
	  echo ""; } | doxygen - >$(cltest2_BUILDDIR)/doxygen.log 2>&1

ARTIFACT+=cltest2
ARTIFACT_DOCS+=cltest2_docs

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
