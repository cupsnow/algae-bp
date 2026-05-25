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
ARTIFACT_EGL:=
ARTIFACT_INSTALL:=
ARTIFACT_DOXYGEN:=

.DEFAULT_GOAL:=artifact

#------------------------------------
#
mesa3d_LDFLAGS+=-L$(BUILD_SYSROOT)/usr/lib64 -L$(BUILD_SYSROOT)/lib64 -L$(BUILD_SYSROOT)/usr/lib -L$(BUILD_SYSROOT)/lib -lEGL -lGLESv2 -lgbm -lgallium-26.1.0-devel -ldrm -lexpat -lz
mesa3d_CPPFLAGS+=-I$(BUILD_SYSROOT)/usr/include -I$(BUILD_SYSROOT)/usr/include/drm -I$(BUILD_SYSROOT)/include -I$(BUILD_SYSROOT)/include/drm

libnl_LDFLAGS+=-L$(BUILD_SYSROOT)/usr/lib64 -L$(BUILD_SYSROOT)/lib64 -L$(BUILD_SYSROOT)/usr/lib -L$(BUILD_SYSROOT)/lib -lnl-3 -lnl-genl-3
libnl_CPPFLAGS+=-I$(BUILD_SYSROOT)/usr/include/libnl3 -I$(BUILD_SYSROOT)/usr/include -I$(BUILD_SYSROOT)/include/libnl3 -I$(BUILD_SYSROOT)/include

TESTER1_ALOE_SRCS+=$(wildcard \
  $(PROJDIR)/package/aloe/*.cpp \
  $(PROJDIR)/package/aloe/*.c \
  $(PROJDIR)/package/aloe/sys_linux/*.cpp \
  $(PROJDIR)/package/aloe/sys_linux/*.c)

#------------------------------------
# $(eval $(call TESTER1_APP2,tester1_ev3,$(TESTER1_ALOE_SRCS) ev3_main.cpp))
#
define TESTER1_APP2
$(1)_BIN?=$(1)

$(1)_BUILDDIR?=$$(PROJDIR)/build/$(1)$$(or $$(APP_BUILD:%=-%),$$(APP_PLATFORM:%=-%))
$(1)_SRCS+=$$(abspath $(2))
$(1)_OBJS+=$$(patsubst $$(PROJDIR)/package/%,$$($(1)_BUILDDIR)/objs/%.o,$$(filter %.c %.cpp,$$($(1)_SRCS)))

$(1)_OBJDEPS+=$$(addsuffix .d,$$($(1)_OBJS))
$(1)_GENDIR+=$$(sort $$($(1)_BUILDDIR) \
  $$(dir $$($(1)_OBJS)) \
)

GENDIR+=$$($(1)_GENDIR)

.PHONY: $$($(1)_BIN)
$$($(1)_BIN): $$($(1)_BUILDDIR)/$$($(1)_BIN)

$$($(1)_BUILDDIR)/$$($(1)_BIN): $$($(1)_OBJS) | $$(filter .a .o,$$($(1)_SRCS))
	$$(if $$(filter %.cpp.o,$$^),$$(C++),$$(CC)) -o $$@ \
	    $$(filter %.o %.a,$$^) $$(or $$($(1)_LDFLAGS),$$(LDFLAGS))

$$(filter $$($(1)_BUILDDIR)/objs/%.c.o,$$($(1)_OBJS)): $$($(1)_BUILDDIR)/objs/%.o: $$(PROJDIR)/package/% | $$($(1)_GENDIR)
	$$(CC) -c -o $$@ $$< $$(DEPFLAGS) $$(or $$($(1)_CPPFLAGS),$$(CPPFLAGS)) \
	    $$(or $$($(1)_CFLAGS),$$(CFLAGS))

$$(filter $$($(1)_BUILDDIR)/objs/%.cpp.o,$$($(1)_OBJS)): $$($(1)_BUILDDIR)/objs/%.o: $$(PROJDIR)/package/% | $$($(1)_GENDIR)
	$$(C++) -c -o $$@ $$< $$(DEPFLAGS) $$(or $$($(1)_CPPFLAGS),$$(CPPFLAGS)) \
	    $$(or $$($(1)_CXXFLAGS),$$(CXXFLAGS))

-include $$(sort $$($(1)_OBJDEPS))
# end of TESTER1_APP2
endef

#------------------------------------
#
tester1_ev3_ATTR+=USE_WIFIMGR
tester1_ev3_SRCS+=$(abspath $(TESTER1_ALOE_SRCS))
tester1_ev3_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX=1
ifneq ($(filter USE_WIFIMGR,$(tester1_ev3_ATTR)),)
tester1_ev3_SRCS+=$(abspath wifi.cpp)
tester1_ev3_CPPFLAGS+=-DUSE_WIFIMGR=1 $(libnl_CPPFLAGS)
tester1_ev3_LDFLAGS+=$(libnl_LDFLAGS)
endif

$(eval $(call TESTER1_APP2,tester1_ev3, \
    ev3_main.cpp mgmt.cpp cli.cpp ipc.cpp))

ARTIFACT+=$(tester1_ev3_BIN)

#------------------------------------
#
GENDIR+=$(BUILDDIR)/tester_aloe-docs
tester_aloe_doxygen: | $(BUILDDIR)/tester_aloe-docs
	cd $(PROJDIR)/package/aloe \
	  && { cat $(PROJDIR)/builder/doxyfile \
	    && echo "PROJECT_NAME=aloe" \
		&& echo "PROJECT_NUMBER=0.0.1" \
		&& echo "INPUT=include" \
		&& echo "OUTPUT_DIRECTORY=$(BUILDDIR)/aloe-docs" \
		&& echo ""; } | doxygen - >$(BUILDDIR)/doxygen.log 2>&1

ARTIFACT_DOXYGEN+=tester_aloe_doxygen

#------------------------------------
#
tester_fb2_SRCS+=$(abspath $(TESTER1_ALOE_SRCS))
tester_fb2_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX=1

$(eval $(call TESTER1_APP2,tester_fb2, \
    tester_fb1.c))

ARTIFACT+=$(tester_fb2_BIN)

#------------------------------------
#
#ARTIFACT+=ARTIFACT_EGL
artifact: $(ARTIFACT) $(ARTIFACT_DOXYGEN)

-include $(sort $(foreach iter,$(ARTIFACT),$(${iter}_DEPS)))

#------------------------------------
#
$(sort $(GENDIR)):
	$(MKDIR) $@

#------------------------------------
#------------------------------------
#------------------------------------
#
