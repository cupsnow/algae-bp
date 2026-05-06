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
define TESTER1_OBJ2
$(1)_BUILDDIR?=$$(BUILDDIR)/tester1$$(or $$(APP_BUILD:%=-%),$$(APP_PLATFORM:%=-%))
# $(1)_BIN?=$(1)
$(1)_SRCS+=$(2)
$(1)_OBJS+=$$(patsubst %,$$($(1)_BUILDDIR)/%.o,$$(filter %.c %.cpp,$$($(1)_SRCS)))
$(1)_DEPS+=$$($(1)_OBJS:%=%.d)

GENDIR+=$$($(1)_BUILDDIR)

$$(filter %.c.o,$$($(1)_OBJS)): $$($(1)_BUILDDIR)/%.c.o: %.c
	[ -d "$$(@D)" ] || $$(MKDIR) $$(@D)
	$$(CC) -c -o $$@ $$< $$(DEPFLAGS) $$(or $$($(1)_CPPFLAGS),$$(CPPFLAGS)) \
	    $$(or $$($(1)_CFLAGS),$$(CFLAGS))

$$(filter %.cpp.o,$$($(1)_OBJS)): $$($(1)_BUILDDIR)/%.cpp.o: %.cpp
	[ -d "$$(@D)" ] || $$(MKDIR) $$(@D)
	$$(C++) -c -o $$@ $$< $$(DEPFLAGS) $$(or $$($(1)_CPPFLAGS),$$(CPPFLAGS)) \
	    $$(or $$($(1)_CXXFLAGS),$$(CXXFLAGS))
# end of TESTER1_OBJ2
endef

define TESTER1_APP2
$(call TESTER1_OBJ2,$(1),$(2),$(3),$(4))
$(1)_BIN?=$(1)

.PHONY: $(1)
$(1): $$($(1)_BUILDDIR)/$$($(1)_BIN)

$$($(1)_BUILDDIR)/$$($(1)_BIN): $$($(1)_OBJS) $$(filter %.o %.a,$$($(1)_SRCS))
	$$(C++) -o $$($(1)_BUILDDIR)/$$($(1)_BIN) $$(filter %.o %.a,$$^) \
	    $$(or $$($(1)_LDFLAGS),$$(LDFLAGS))
# end of TESTER1_APP2 for $1
endef

#------------------------------------
# -Wl,--start-group ... -Wl,--end-group
#
$(eval $(call TESTER1_OBJ2,tester_aloe, \
    $(wildcard $(addprefix $(PROJDIR)/package/aloe/,*.cpp *.c)) \
    $(wildcard $(addprefix $(PROJDIR)/package/aloe/sys_linux/,*.cpp *.c)) \
))
tester_aloe_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX=1

GENDIR+=$(BUILDDIR)/tester_aloe-docs
tester_aloe_doxygen: | $(BUILDDIR)/tester_aloe-docs
	cd $(PROJDIR)/package/aloe && \
	  { cat Doxyfile && \
	  echo "PROJECT_NUMBER=0.0.1" && \
	  echo "INPUT=include" &&  \
	  echo "OUTPUT_DIRECTORY=$(BUILDDIR)/aloe-docs" && \
	  echo ""; } | doxygen - >$(BUILDDIR)/doxygen.log 2>&1

ARTIFACT_DOXYGEN+=tester_aloe_doxygen

#------------------------------------
#
ifneq ($(strip $(BUILD_SYSROOT)),)
mesa3d_LDFLAGS+=-L$(BUILD_SYSROOT)/usr/lib64 -L$(BUILD_SYSROOT)/lib64 -L$(BUILD_SYSROOT)/usr/lib -L$(BUILD_SYSROOT)/lib -lEGL -lGLESv2 -lgbm -lgallium-26.1.0-devel -ldrm -lexpat -lz
mesa3d_CPPFLAGS+=-I$(BUILD_SYSROOT)/usr/include -I$(BUILD_SYSROOT)/usr/include/drm -I$(BUILD_SYSROOT)/include -I$(BUILD_SYSROOT)/include/drm

libnl_LDFLAGS+=-L$(BUILD_SYSROOT)/usr/lib64 -L$(BUILD_SYSROOT)/lib64 -L$(BUILD_SYSROOT)/usr/lib -L$(BUILD_SYSROOT)/lib -lnl-3 -lnl-genl-3
libnl_CPPFLAGS+=-I$(BUILD_SYSROOT)/usr/include/libnl3 -I$(BUILD_SYSROOT)/usr/include -I$(BUILD_SYSROOT)/include/libnl3 -I$(BUILD_SYSROOT)/include
endif

#------------------------------------
#
tester_ev3_LDFLAGS+=$(tester_aloe_LDFLAGS)
tester_ev3_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_ev3, \
    $(tester_aloe_OBJS) \
    ev3_main.cpp mgmt.cpp cli.cpp ipc.cpp \
))

ARTIFACT+=tester_ev3

#------------------------------------
#
tester_wifi2_LDFLAGS+=$(tester_aloe_LDFLAGS) $(libnl_LDFLAGS)
tester_wifi2_CPPFLAGS+=$(tester_aloe_CPPFLAGS) $(libnl_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_wifi2, \
    $(tester_aloe_OBJS) \
    wifi2_main.cpp mgmt.cpp cli.cpp ipc.cpp wifi.cpp \
))

ARTIFACT+=tester_wifi2

#------------------------------------
#
tester_fb2_LDFLAGS+=$(tester_aloe_LDFLAGS)
tester_fb2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_fb2, \
    $(tester_aloe_OBJS) \
    tester_fb1.c \
))

ARTIFACT+=tester_fb2

#------------------------------------
#
tester_egl2_LDFLAGS+=$(mesa3d_LDFLAGS) $(tester_aloe_LDFLAGS)
tester_egl2_CPPFLAGS+=$(mesa3d_CPPFLAGS) $(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_egl2, \
    $(tester_aloe_OBJS) \
    tester_egl2.c))

ARTIFACT_EGL+=tester_egl2

#------------------------------------
#
tester_egl3_LDFLAGS+=$(mesa3d_LDFLAGS) $(tester_aloe_LDFLAGS)
tester_egl3_CPPFLAGS+=$(mesa3d_CPPFLAGS) $(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_egl3, \
  $(tester_aloe_OBJS) \
  tester_egl3.c))

ARTIFACT_EGL+=tester_egl3

#------------------------------------
#
tester_egl4_LDFLAGS+=$(mesa3d_LDFLAGS) $(tester_aloe_LDFLAGS)
tester_egl4_CPPFLAGS+=$(mesa3d_CPPFLAGS) $(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_egl4, \
  $(tester_aloe_OBJS) \
  tester_egl4.c))

ARTIFACT_EGL+=tester_egl4

#------------------------------------
#
tester_egl5_LDFLAGS+=$(mesa3d_LDFLAGS) $(tester_aloe_LDFLAGS)
tester_egl5_CPPFLAGS+=$(mesa3d_CPPFLAGS) $(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_egl5, \
  $(tester_aloe_OBJS) \
  tester_egl5.c))

ARTIFACT_EGL+=tester_egl5

#------------------------------------
#
tester_egl6_LDFLAGS+=$(mesa3d_LDFLAGS) $(tester_aloe_LDFLAGS)
tester_egl6_CPPFLAGS+=$(mesa3d_CPPFLAGS) $(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_egl6, \
  $(tester_aloe_OBJS) \
  tester_egl6.c))

ARTIFACT_EGL+=tester_egl6

#------------------------------------
#
tester_egl7_LDFLAGS+=$(mesa3d_LDFLAGS) $(tester_aloe_LDFLAGS)
tester_egl7_CPPFLAGS+=$(mesa3d_CPPFLAGS) $(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_egl7, \
  $(tester_aloe_OBJS) \
  tester_egl7.c))

ARTIFACT_EGL+=tester_egl7

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
