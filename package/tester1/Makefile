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

ARTIFACT_DOXYGEN+=tester_aloe_doxygen

#------------------------------------
# $(eval $(call TESTER1_APP2,tester_ev3,$(tester_aloe_SRCS) ev3_main.cpp))
#
define TESTER1_APP2
$(1)_BIN?=$(1)
$(1)_BUILDDIR?=$$(PROJDIR)/build/$(1)$$(APP_PLATFORM:%=-%)
$(1)_SRCS+=$(2)
$(1)_OBJS+=$$(patsubst %,$$($(1)_BUILDDIR)/objs/%.o,$$(filter %.c %.cpp,$$($(1)_SRCS)))
$(1)_OBJDEPS+=$$(addsuffix .d,$$($(1)_OBJS))
$(1)_GENDIR+=$$(sort $$($(1)_BUILDDIR) $$(dir $$($(1)_OBJS)))

GENDIR+=$$($(1)_GENDIR)

.PHONY: $$($(1)_BIN)
$$($(1)_BIN): $$($(1)_BUILDDIR)/$$($(1)_BIN)

$$($(1)_BUILDDIR)/$$($(1)_BIN): $$($(1)_OBJS) | $$(filter %.a %.o,$$($(1)_SRCS))
	$$(if $$(filter %.cpp.o,$$^ $$|),$$(C++),$$(CC)) -o $$@ \
	    $$(filter %.o %.a,$$^ $$|) $$(or $$($(1)_LDFLAGS),$$(LDFLAGS))

$$(filter $$($(1)_BUILDDIR)/objs/%.c.o,$$($(1)_OBJS)): $$($(1)_BUILDDIR)/objs/%.o : % | $$($(1)_GENDIR)
	$$(CC) -c -o $$@ $$< $$(DEPFLAGS) $$(or $$($(1)_CPPFLAGS),$$(CPPFLAGS)) \
	    $$(or $$($(1)_CFLAGS),$$(CFLAGS))

$$(filter $$($(1)_BUILDDIR)/objs/%.cpp.o,$$($(1)_OBJS)): $$($(1)_BUILDDIR)/objs/%.o : % | $$($(1)_GENDIR)
	$$(C++) -c -o $$@ $$< $$(DEPFLAGS) $$(or $$($(1)_CPPFLAGS),$$(CPPFLAGS)) \
	    $$(or $$($(1)_CXXFLAGS),$$(CXXFLAGS))

-include $$(sort $$($(1)_OBJDEPS))
# end of TESTER1_APP2
endef

#------------------------------------
#
tester_cli2_ATTR+=

tester_cli2_SRCS+=$(tester_aloe_SRCS)
tester_cli2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

tester_cli2_CPPFLAGS+=$(CPPFLAGS)
tester_cli2_CFLAGS+=$(CFLAGS)
tester_cli2_CXXFLAGS+=$(CXXFLAGS)
tester_cli2_LDFLAGS+=$(LDFLAGS)

$(eval $(call TESTER1_APP2,tester_cli2, \
    tester_cli2.cpp mgmt.cpp cli.cpp ipc.cpp priv.cpp settings.cpp))

ARTIFACT+=$(tester_cli2_BIN)

#------------------------------------
#
tester_ev3_ATTR+=USE_WIFIMGR USE_WPASUPCLIENT USE_V4L2 USE_X264

tester_ev3_SRCS+=$(tester_aloe_SRCS)
tester_ev3_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

ifneq ($(filter USE_WIFIMGR,$(tester_ev3_ATTR)),)
tester_ev3_SRCS+=wifi.cpp
tester_ev3_CPPFLAGS+=-DUSE_WIFIMGR=1 $(libnl_CPPFLAGS)
tester_ev3_CXXFLAGS+=$(libnl_CXXFLAGS)
tester_ev3_LDFLAGS+=$(libnl_LDFLAGS)

ifneq ($(filter USE_WPASUPCLIENT,$(tester_ev3_ATTR)),)
tester_ev3_CPPFLAGS+=-DUSE_WPASUPCLIENT=1 -I$(BUILD_SYSROOT)/include
tester_ev3_SRCS+=$(BUILD_SYSROOT)/lib/libwpa_client.a

# end of USE_WPASUPCLIENT
endif

# end of USE_WIFIMGR
endif

ifneq ($(filter USE_V4L2,$(tester_ev3_ATTR)),)
tester_ev3_SRCS+=v4l2.cpp
tester_ev3_CPPFLAGS+=-DUSE_V4L2=1
# end of USE_V4L2
endif

ifneq ($(filter USE_X264,$(tester_ev3_ATTR)),)
tester_ev3_SRCS+=x264_encoder.cpp
tester_ev3_CPPFLAGS+=-DUSE_X264=1 $(x264_CPPFLAGS)
tester_ev3_LDFLAGS+=$(x264_LDFLAGS)
# end of USE_X264
endif

tester_ev3_CPPFLAGS+=$(CPPFLAGS)
tester_ev3_CFLAGS+=$(CFLAGS)
tester_ev3_CXXFLAGS+=$(CXXFLAGS)
tester_ev3_LDFLAGS+=$(LDFLAGS)

$(eval $(call TESTER1_APP2,tester_ev3, \
    tester_ev3.cpp mgmt.cpp cli.cpp ipc.cpp priv.cpp settings.cpp))

ARTIFACT+=$(tester_ev3_BIN)

#------------------------------------
#
tester_rtsp2_SRCS+=$(tester_aloe_SRCS)
tester_rtsp2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

$(foreach pkg,live555 openssl x264, \
  $(eval tester_rtsp2_CPPFLAGS+=$($(pkg)_CPPFLAGS)) \
  $(eval tester_rtsp2_LDFLAGS+=$($(pkg)_LDFLAGS)))

tester_rtsp2_CPPFLAGS+=$(CPPFLAGS)
tester_rtsp2_CFLAGS+=$(CFLAGS)
tester_rtsp2_CXXFLAGS+=$(CXXFLAGS)
tester_rtsp2_LDFLAGS+=$(LDFLAGS)

$(eval $(call TESTER1_APP2,tester_rtsp2, \
    tester_rtsp2.cpp))

ARTIFACT+=$(tester_rtsp2_BIN)

#------------------------------------
#
tester_v4l2r2_SRCS+=$(tester_aloe_SRCS)
tester_v4l2r2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

$(foreach pkg,live555 openssl x264, \
  $(eval tester_v4l2r2_CPPFLAGS+=$($(pkg)_CPPFLAGS)) \
  $(eval tester_v4l2r2_LDFLAGS+=$($(pkg)_LDFLAGS)))

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

$(foreach pkg,live555 openssl x264, \
  $(eval tester_v4l2rtsp_CPPFLAGS+=$($(pkg)_CPPFLAGS)) \
  $(eval tester_v4l2rtsp_LDFLAGS+=$($(pkg)_LDFLAGS)))

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
tester_v4l2rtsp2_SRCS+=$(abspath $(wildcard \
  $(PROJDIR)/package/aloe/simd/*.cpp \
  $(PROJDIR)/package/aloe/simd/*.c \
))

tester_v4l2rtsp2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

$(foreach pkg,live555 openssl x264, \
  $(eval tester_v4l2rtsp2_CPPFLAGS+=$($(pkg)_CPPFLAGS)) \
  $(eval tester_v4l2rtsp2_LDFLAGS+=$($(pkg)_LDFLAGS)))

ifneq ($(filter bp qemuarm64,$(APP_PLATFORM)),)
tester_v4l2rtsp2_CFLAGS+=-O3 -march=armv8-a+simd
tester_v4l2rtsp2_CXXFLAGS+=-O3 -march=armv8-a+simd
tester_v4l2rtsp2_LDFLAGS+=-O3 -march=armv8-a+simd
else ifneq ($(filter bb,$(APP_PLATFORM)),)
tester_v4l2rtsp2_CFLAGS+=-O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard 
tester_v4l2rtsp2_CXXFLAGS+=-O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard 
tester_v4l2rtsp2_LDFLAGS+=-O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard 
else
tester_v4l2rtsp2_CFLAGS+=-O3 -mavx2
tester_v4l2rtsp2_CXXFLAGS+=-O3 -mavx2
tester_v4l2rtsp2_LDFLAGS+=-O3 -mavx2
endif

tester_v4l2rtsp2_CPPFLAGS+=$(CPPFLAGS)
tester_v4l2rtsp2_CFLAGS+=$(CFLAGS)
tester_v4l2rtsp2_CXXFLAGS+=$(CXXFLAGS)
tester_v4l2rtsp2_LDFLAGS+=$(LDFLAGS)

$(eval $(call TESTER1_APP2,tester_v4l2rtsp2, \
    tester_v4l2rtsp2.cpp \
	v4l2_capture.cpp \
	x264_encoder.cpp \
	))

ARTIFACT+=$(tester_v4l2rtsp2_BIN)

#------------------------------------
#
tester_srggb10_SRCS+=$(abspath $(tester_aloe_SRCS))
tester_srggb10_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

#$(foreach pkg,live555 openssl x264, \
#  $(eval tester_srggb10_CPPFLAGS+=$($(pkg)_CPPFLAGS)) \
#  $(eval tester_srggb10_LDFLAGS+=$($(pkg)_LDFLAGS)))

tester_srggb10_CPPFLAGS+=$(CPPFLAGS)
tester_srggb10_CFLAGS+=$(CFLAGS)
tester_srggb10_CXXFLAGS+=$(CXXFLAGS)
tester_srggb10_LDFLAGS+=$(LDFLAGS)

$(eval $(call TESTER1_APP2,tester_srggb10, \
    tester_srggb10.cpp \
	))

ARTIFACT+=$(tester_srggb10_BIN)

#------------------------------------
#
tester_rg10r2_SRCS+=$(abspath $(tester_aloe_SRCS))
tester_rg10r2_CPPFLAGS+=$(tester_aloe_CPPFLAGS)

#$(foreach pkg,live555 openssl x264, \
#  $(eval tester_rg10r2_CPPFLAGS+=$($(pkg)_CPPFLAGS)) \
#  $(eval tester_rg10r2_LDFLAGS+=$($(pkg)_LDFLAGS)))

tester_rg10r2_CPPFLAGS+=$(CPPFLAGS)
tester_rg10r2_CFLAGS+=$(CFLAGS)
tester_rg10r2_CXXFLAGS+=$(CXXFLAGS)
tester_rg10r2_LDFLAGS+=$(LDFLAGS)

$(eval $(call TESTER1_APP2,tester_rg10r2, \
    tester_rg10r2.cpp \
	))

ARTIFACT+=$(tester_rg10r2_BIN)

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
