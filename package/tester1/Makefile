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
-include $(PROJDIR)/site.mk

CPPFLAGS+=
CFLAGS+=-g
CXXFLAGS+=-g
LDFLAGS+=

GENDIR:=

ARTIFACT:=
ARTIFACT_EGL:=
ARTIFACT_INSTALL:=
ARTIFACT_DOXYGEN:=

.DEFAULT_GOAL:=artifact

ifneq ($(filter sa7715,$(APP_PLATFORM)),)
SA7715_SYSROOT=$(HOME)/02_dev/eveplay-ws/build/sysroot-sa7715
else ifneq ($(filter esh,$(APP_PLATFORM)),)
TOOLCHAIN_SYSROOT?=$(abspath $(shell $(CROSS_COMPILE)gcc -print-sysroot))
ESH_SYSROOT=$(HOME)/02_exdev2/agt-ws/esghub/buildroot/output/staging
CXXFLAGS+=-std=gnu++11 --sysroot=$(TOOLCHAIN_SYSROOT)
LDFLAGS+=--sysroot=$(TOOLCHAIN_SYSROOT)
else ifneq ($(filter iq9,$(APP_PLATFORM)),)
CPPFLAGS+=--sysroot=$(TOOLCHAIN_SYSROOT) -I$(TOOLCHAIN_SYSROOT)/include -I$(TOOLCHAIN_SYSROOT)/usr/include
LDFLAGS+=--sysroot=$(TOOLCHAIN_SYSROOT) -L$(TOOLCHAIN_SYSROOT)/lib -L$(TOOLCHAIN_SYSROOT)/usr/lib
endif

#------------------------------------
#
mesa3d_LDFLAGS+=-L$(BUILD_SYSROOT)/usr/lib64 -L$(BUILD_SYSROOT)/lib64 -L$(BUILD_SYSROOT)/usr/lib -L$(BUILD_SYSROOT)/lib -lEGL -lGLESv2 -lgbm -lgallium-26.1.0-devel -ldrm -lexpat -lz
mesa3d_CPPFLAGS+=-I$(BUILD_SYSROOT)/usr/include -I$(BUILD_SYSROOT)/usr/include/drm -I$(BUILD_SYSROOT)/include -I$(BUILD_SYSROOT)/include/drm

openssl_LDFLAGS+=-L$(BUILD_SYSROOT)/lib -lssl -lcrypto
openssl_CPPFLAGS+=-I$(BUILD_SYSROOT)/include

ifneq ($(filter sa7715,$(APP_PLATFORM)),)
libnl_LDFLAGS+=-L$(SA7715_SYSROOT)/usr/lib64 -L$(SA7715_SYSROOT)/lib64 -L$(SA7715_SYSROOT)/usr/lib -L$(SA7715_SYSROOT)/lib -lnl-3 -lnl-genl-3
libnl_CPPFLAGS+=-I$(SA7715_SYSROOT)/usr/include/libnl3 -I$(SA7715_SYSROOT)/usr/include -I$(SA7715_SYSROOT)/include/libnl3 -I$(SA7715_SYSROOT)/include
else ifneq ($(filter esh,$(APP_PLATFORM)),)
libnl_LDFLAGS+=-L$(ESH_SYSROOT)/usr/lib64 -L$(ESH_SYSROOT)/lib64 -L$(ESH_SYSROOT)/usr/lib -L$(ESH_SYSROOT)/lib -lnl-3 -lnl-genl-3
libnl_CPPFLAGS+=-I$(ESH_SYSROOT)/usr/include/libnl3 -I$(ESH_SYSROOT)/usr/include -I$(ESH_SYSROOT)/include/libnl3 -I$(ESH_SYSROOT)/include
else ifneq ($(filter iq9,$(APP_PLATFORM)),)
libnl_LDFLAGS+=-L$(TOOLCHAIN_SYSROOT)/usr/lib64 -L$(TOOLCHAIN_SYSROOT)/lib64 -L$(TOOLCHAIN_SYSROOT)/usr/lib -L$(TOOLCHAIN_SYSROOT)/lib -lnl-3 -lnl-genl-3
libnl_CPPFLAGS+=-I$(TOOLCHAIN_SYSROOT)/usr/include/libnl3 -I$(TOOLCHAIN_SYSROOT)/usr/include -I$(TOOLCHAIN_SYSROOT)/include/libnl3 -I$(TOOLCHAIN_SYSROOT)/include
else
libnl_LDFLAGS+=-L$(BUILD_SYSROOT)/usr/lib64 -L$(BUILD_SYSROOT)/lib64 -L$(BUILD_SYSROOT)/usr/lib -L$(BUILD_SYSROOT)/lib -lnl-3 -lnl-genl-3
libnl_CPPFLAGS+=-I$(BUILD_SYSROOT)/usr/include/libnl3 -I$(BUILD_SYSROOT)/usr/include -I$(BUILD_SYSROOT)/include/libnl3 -I$(BUILD_SYSROOT)/include
endif

live555_ELEM=liveMedia groupsock BasicUsageEnvironment UsageEnvironment
live555_CPPFLAGS+=$(addprefix -I$(BUILD_SYSROOT)/include/,$(live555_ELEM))
#live555_LDFLAGS+=-L$(BUILD_SYSROOT)/lib $(addprefix -l,$(live555_ELEM))
live555_LDFLAGS+=$(addsuffix .a,$(addprefix $(BUILD_SYSROOT)/lib/lib,$(live555_ELEM)))

x264_CPPFLAGS=-I$(BUILD_SYSROOT)/include
x264_LDFLAGS+=-L$(BUILD_SYSROOT)/lib -lx264

tester_aloe_CPPFLAGS+=-I$(PROJDIR)/package/aloe/include -DALOE_SYS_LINUX=1
tester_aloe_SRCS+=$(wildcard \
  $(PROJDIR)/package/aloe/*.cpp \
  $(PROJDIR)/package/aloe/*.c \
  $(PROJDIR)/package/aloe/sys_linux/*.cpp \
  $(PROJDIR)/package/aloe/sys_linux/*.c)

#------------------------------------
# $(eval $(call TESTER1_APP2,tester_ev3,$(tester_aloe_SRCS) ev3_main.cpp))
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

$$($(1)_BUILDDIR)/$$($(1)_BIN): $$($(1)_OBJS) | $$(filter %.a %.o,$$($(1)_SRCS))
	$$(if $$(filter %.cpp.o,$$^ $$|),$$(C++),$$(CC)) -o $$@ \
	    $$(filter %.o %.a,$$^ $$|) $$(or $$($(1)_LDFLAGS),$$(LDFLAGS))

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
tester_ev3_ATTR+=USE_WIFIMGR USE_WPASUPCLIENT

tester_ev3_CPPFLAGS+=$(CPPFLAGS)
tester_ev3_CFLAGS+=$(CFLAGS)
tester_ev3_CXXFLAGS+=$(CXXFLAGS)
tester_ev3_LDFLAGS+=$(LDFLAGS)

tester_ev3_SRCS+=$(abspath $(tester_aloe_SRCS))
tester_ev3_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

ifneq ($(filter USE_WIFIMGR,$(tester_ev3_ATTR)),)
tester_ev3_SRCS+=$(abspath wifi.cpp)
tester_ev3_CPPFLAGS+=-DUSE_WIFIMGR=1 $(libnl_CPPFLAGS)
tester_ev3_CXXFLAGS+=$(libnl_CXXFLAGS)
tester_ev3_LDFLAGS+=$(libnl_LDFLAGS)

ifneq ($(filter USE_WPASUPCLIENT,$(tester_ev3_ATTR)),)
tester_ev3_CPPFLAGS+=-DUSE_WPASUPCLIENT=1 -I$(BUILD_SYSROOT)/include
tester_ev3_SRCS+=$(BUILD_SYSROOT)/lib/libwpa_client.a
endif

#end of USE_WIFIMGR
endif

$(eval $(call TESTER1_APP2,tester_ev3, \
    ev3_main.cpp mgmt.cpp cli.cpp ipc.cpp priv.cpp settings.cpp))

ARTIFACT+=$(tester_ev3_BIN)

#------------------------------------
#
tester_rtsp2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)
tester_rtsp2_CPPFLAGS+=$(live555_CPPFLAGS) $(openssl_CPPFLAGS) $(CPPFLAGS)
tester_rtsp2_CFLAGS+=$(CFLAGS)
tester_rtsp2_CXXFLAGS+=$(CXXFLAGS)
tester_rtsp2_LDFLAGS+=$(live555_LDFLAGS) $(openssl_LDFLAGS) $(LDFLAGS)

tester_rtsp2_SRCS+=$(abspath $(tester_aloe_SRCS))

$(eval $(call TESTER1_APP2,tester_rtsp2, \
    tester_rtsp2.cpp))

ARTIFACT+=$(tester_rtsp2_BIN)

#------------------------------------
#
tester_v4l2r2_SRCS+=$(abspath $(tester_aloe_SRCS))
tester_v4l2r2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

tester_v4l2r2_CPPFLAGS+=$(live555_CPPFLAGS) $(openssl_CPPFLAGS) $(x264_CPPFLAGS)
tester_v4l2r2_LDFLAGS+=$(live555_LDFLAGS) $(openssl_LDFLAGS) $(x264_LDFLAGS)

tester_v4l2r2_CPPFLAGS+=$(CPPFLAGS)
tester_v4l2r2_CFLAGS+=$(CFLAGS)
tester_v4l2r2_CXXFLAGS+=$(CXXFLAGS)
tester_v4l2r2_LDFLAGS+=$(LDFLAGS)

$(eval $(call TESTER1_APP2,tester_v4l2r2, \
    tester_v4l2r2.cpp \
	v4l2_capture.cpp \
	x264_encoder.cpp \
	))

ARTIFACT+=$(tester_v4l2r2_BIN)

#------------------------------------
#
tester_v4l2rtsp_SRCS+=$(abspath $(tester_aloe_SRCS))
tester_v4l2rtsp_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

tester_v4l2rtsp_CPPFLAGS+=$(live555_CPPFLAGS) $(openssl_CPPFLAGS) $(x264_CPPFLAGS)
tester_v4l2rtsp_LDFLAGS+=$(live555_LDFLAGS) $(openssl_LDFLAGS) $(x264_LDFLAGS)

tester_v4l2rtsp_CPPFLAGS+=$(CPPFLAGS)
tester_v4l2rtsp_CFLAGS+=$(CFLAGS)
tester_v4l2rtsp_CXXFLAGS+=$(CXXFLAGS)
tester_v4l2rtsp_LDFLAGS+=$(LDFLAGS)

$(eval $(call TESTER1_APP2,tester_v4l2rtsp, \
    tester_v4l2rtsp.cpp \
	v4l2_capture.cpp \
	x264_encoder.cpp \
	))

ARTIFACT+=$(tester_v4l2rtsp_BIN)

#------------------------------------
#
tester_v4l2rtsp2_SRCS+=$(abspath $(tester_aloe_SRCS))
tester_v4l2rtsp2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

tester_v4l2rtsp2_CPPFLAGS+=$(live555_CPPFLAGS) $(openssl_CPPFLAGS) $(x264_CPPFLAGS)
tester_v4l2rtsp2_LDFLAGS+=$(live555_LDFLAGS) $(openssl_LDFLAGS) $(x264_LDFLAGS)

tester_v4l2rtsp2_CPPFLAGS+=$(CPPFLAGS)
tester_v4l2rtsp2_CFLAGS+=$(CFLAGS)
tester_v4l2rtsp2_CXXFLAGS+=$(CXXFLAGS)
tester_v4l2rtsp2_LDFLAGS+=$(LDFLAGS)

$(eval $(call TESTER1_APP2,tester_v4l2rtsp2, \
    tester_v4l2rtsp2.cpp \
	v4l2_capture.cpp \
	x264_encoder.cpp \
	))

#ARTIFACT+=$(tester_v4l2rtsp2_BIN)

#------------------------------------
#
tester_aloe_DOCSDIR?=$(BUILDDIR)/aloe-docs
GENDIR+=$(tester_aloe_DOCSDIR)
tester_aloe_doxygen: | $(tester_aloe_DOCSDIR)
	cd $(PROJDIR)/package/aloe \
	  && { cat $(PROJDIR)/builder/doxyfile \
	    && echo "PROJECT_NAME=aloe" \
		&& echo "PROJECT_NUMBER=0.0.1" \
		&& echo "INPUT=include" \
		&& echo "OUTPUT_DIRECTORY=$(tester_aloe_DOCSDIR)" \
		&& echo ""; } | doxygen - >$(BUILDDIR)/doxygen.log 2>&1

#ARTIFACT_DOXYGEN+=tester_aloe_doxygen

#------------------------------------
#
tester_fb2_SRCS+=$(abspath $(tester_aloe_SRCS))

tester_fb2_CPPFLAGS+=$(CPPFLAGS)
tester_fb2_CFLAGS+=$(CFLAGS)
tester_fb2_CXXFLAGS+=$(CXXFLAGS)
tester_fb2_LDFLAGS+=$(LDFLAGS)

tester_fb2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_fb2, \
    tester_fb1.c))

ARTIFACT+=$(tester_fb2_BIN)

#------------------------------------
#
live555_DIR=$(abspath ../live555)
live555_BUILDDIR=$(BUILDDIR)/live555-$(APP_BUILD)
live555_DESTDIR=$(live555_BUILDDIR)/destdir

ifneq ($(filter sa7715,$(APP_PLATFORM)),)
live555_CPPFLAGS+=-I$(SA7715_SYSROOT)/usr/include/libnl3 -I$(SA7715_SYSROOT)/usr/include -I$(SA7715_SYSROOT)/include/libnl3 -I$(SA7715_SYSROOT)/include
live555_LDFLAGS+=-L$(SA7715_SYSROOT)/usr/lib64 -L$(SA7715_SYSROOT)/lib64 -L$(SA7715_SYSROOT)/usr/lib -L$(SA7715_SYSROOT)/lib -lnl-3 -lnl-genl-3
else
live555_CPPFLAGS+=-I$(BUILD_SYSROOT)/usr/include/libnl3 -I$(BUILD_SYSROOT)/usr/include -I$(BUILD_SYSROOT)/include/libnl3 -I$(BUILD_SYSROOT)/include
live555_LDFLAGS+=-L$(BUILD_SYSROOT)/usr/lib64 -L$(BUILD_SYSROOT)/lib64 -L$(BUILD_SYSROOT)/usr/lib -L$(BUILD_SYSROOT)/lib -lnl-3 -lnl-genl-3
endif

live555_MAKE=$(MAKE) \
  C_COMPILER=$(CC) CPLUSPLUS_COMPILER=$(C++) LINK="$(C++) -o " \
  LIBRARY_LINK="$(AR) cr " CPPFLAGS="$(live555_CPPFLAGS)" \
  LDFLAGS="$(live555_LDFLAGS)" -C $(live555_BUILDDIR)

GENDIR+=$(live555_BUILDDIR) $(live555_DESTDIR)

$(live555_BUILDDIR)/Makefile: | $(live555_BUILDDIR)
	cp -a $(live555_DIR)/* $(live555_BUILDDIR)/
	cd $(live555_BUILDDIR) && ./genMakefiles algae-bp

live555_install: | $(live555_DESTDIR)
	$(live555_MAKE) DESTDIR=$(live555_DESTDIR) PREFIX=/ install

live555_%: | $(live555_BUILDDIR)/Makefile
	$(live555_MAKE) $*

live555: | $(live555_BUILDDIR)/Makefile
	$(live555_MAKE)

#------------------------------------
#
tester_abc2_CPPFLAGS+=$(CPPFLAGS)
tester_abc2_CFLAGS+=$(CFLAGS)
tester_abc2_CXXFLAGS+=$(CXXFLAGS)
tester_abc2_LDFLAGS+=$(LDFLAGS)

tester_abc2_SRCS+=$(abspath $(tester_aloe_SRCS))
tester_abc2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

$(eval $(call TESTER1_APP2,tester_abc2, \
    tester_abc2.cpp cli.cpp ipc.cpp priv.cpp))

ARTIFACT+=$(tester_abc2_BIN)

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
