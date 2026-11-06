# Minimal Yocto build entry point for BeaglePlay.
#
#   make -f yocto.mk bootstrap
#   make -f yocto.mk all

SHELL:=/bin/bash
.DEFAULT_GOAL:=help

PROJDIR:=$(abspath $(dir $(lastword $(MAKEFILE_LIST))))

APP_PLATFORM?=bp

YOCTO_DIR?=$(PROJDIR)
YOCTO_SOURCES_DIR?=$(YOCTO_DIR)/sources
YOCTO_BUILD_DIR?=$(YOCTO_DIR)/build_yocto-$(APP_PLATFORM)
YOCTO_TEMPLATE_DIR?=$(YOCTO_DIR)/meta-algae/conf/templates/algae-bp
YOCTO_DOWNLOAD_DIR?=$(HOME)/02_dev2/yocto-dl

YOCTO_BRANCH?=scarthgap
POKY_REVISION?=b6ae714a1d1bfae8384654a13da15996cac64712
META_ARM_REVISION?=0a4b911d9fd63ab29c4532a907fae3282dbdcfe4
META_TI_REVISION?=760bcd6af8cb13cf9c56c460e4dd0fdba6ea5b79

POKY_REPOSITORY?=https://git.yoctoproject.org/poky
META_ARM_REPOSITORY?=https://git.yoctoproject.org/meta-arm
META_TI_REPOSITORY?=https://git.yoctoproject.org/meta-ti

HOSTLSB_VER=$(strip $(shell lsb_release -rs))
APPARMOR_RESTRICT_USERNS=$(strip $(shell cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns))

GIT?=git
BITBAKE_TARGET?=algae-busybox-image

CLIARGS_VAL=$(if $(filter x"command line",x"$(strip $(origin $(1)))"),$($(1)))
CLIARGS_VERBOSE=$(or $(call CLIARGS_VAL,V),$(call CLIARGS_VAL,VERBOSE))

RSYNC_VERBOSE+=--debug=FILTER
ifneq ($(filter 1 2 3,$(CLIARGS_VERBOSE)),)
  CP_VERBOSE+=-v
  RM_VERBOSE+=-v
  MV_VERBOSE+=-v
  MKDIR_VERBOSE+=-v
  MESON_VERBOSE+=-v
  RSYNC_VERBOSE+=-v
  CMAKE_MAKE_VERBOSE+=VERBOSE=1
endif

GENDIR:=

.PHONY: help bootstrap all print-config

#------------------------------------
#
PYVENVDIR?=$(PROJDIR)/.venv
PYVENV=. $(PYVENVDIR)/bin/activate

pyvenv $(PYVENVDIR):
	python3 -m venv $(PYVENVDIR)
	if [ -f "requirements.txt" ]; then \
	  $(PYVENV) && pip install -r requirements.txt --upgrade pip; \
	else \
	  $(PYVENV) && pip install --upgrade pip; \
	fi

#------------------------------------
#
help: help_bpyocto

define help_bpyocto
Algae BeaglePlay Yocto build

  make bootstrap  Download layers and create yocto/build
  make all        Build $(BITBAKE_TARGET)

YOCTO_BRANCH=$(YOCTO_BRANCH)
YOCTO_SOURCES_DIR=$(YOCTO_SOURCES_DIR)
YOCTO_BUILD_DIR=$(YOCTO_BUILD_DIR)
YOCTO_DOWNLOAD_DIR=$(YOCTO_DOWNLOAD_DIR)
BITBAKE_TARGET=$(BITBAKE_TARGET)
POKY_REVISION=$(POKY_REVISION)
META_ARM_REVISION=$(META_ARM_REVISION)
META_TI_REVISION=$(META_TI_REVISION)
endef

help_bpyocto:
# any receipe unless make shows "Nothing to be done for xxx"
	@:
	$(info $(help_bpyocto))

#------------------------------------
#
GENDIR+=$(YOCTO_SOURCES_DIR) $(YOCTO_DOWNLOAD_DIR)

$(YOCTO_SOURCES_DIR)/poky/.git: | $(YOCTO_SOURCES_DIR)
	$(GIT) clone --branch "$(YOCTO_BRANCH)" --single-branch \
	    "$(POKY_REPOSITORY)" "$(YOCTO_SOURCES_DIR)/poky"
	$(GIT) -C "$(YOCTO_SOURCES_DIR)/poky" checkout --detach "$(POKY_REVISION)"

$(YOCTO_SOURCES_DIR)/meta-arm/.git: | $(YOCTO_SOURCES_DIR)
	$(GIT) clone --branch "$(YOCTO_BRANCH)" --single-branch \
	    "$(META_ARM_REPOSITORY)" "$(YOCTO_SOURCES_DIR)/meta-arm"
	$(GIT) -C "$(YOCTO_SOURCES_DIR)/meta-arm" checkout --detach "$(META_ARM_REVISION)"

$(YOCTO_SOURCES_DIR)/meta-ti/.git: | $(YOCTO_SOURCES_DIR)
	$(GIT) clone --branch "$(YOCTO_BRANCH)" --single-branch \
	    "$(META_TI_REPOSITORY)" "$(YOCTO_SOURCES_DIR)/meta-ti"
	$(GIT) -C "$(YOCTO_SOURCES_DIR)/meta-ti" checkout --detach "$(META_TI_REVISION)"

bootstrap $(YOCTO_BUILD_DIR): \
      $(YOCTO_SOURCES_DIR)/poky/.git \
      $(YOCTO_SOURCES_DIR)/meta-arm/.git \
      $(YOCTO_SOURCES_DIR)/meta-ti/.git \
	  USERNS_CHECK \
      | $(YOCTO_DOWNLOAD_DIR)
	@test "$$($(GIT) -C "$(YOCTO_SOURCES_DIR)/poky" rev-parse HEAD)" = "$(POKY_REVISION)"
	@test "$$($(GIT) -C "$(YOCTO_SOURCES_DIR)/meta-arm" rev-parse HEAD)" = "$(META_ARM_REVISION)"
	@test "$$($(GIT) -C "$(YOCTO_SOURCES_DIR)/meta-ti" rev-parse HEAD)" = "$(META_TI_REVISION)"
	@mkdir -p $(MK_VERBOSE) "$(YOCTO_BUILD_DIR)"
	@cd "$(YOCTO_SOURCES_DIR)/poky"; \
	  export TEMPLATECONF="$(YOCTO_TEMPLATE_DIR)"; \
	  set -- "$(YOCTO_BUILD_DIR)"; \
	  . ./oe-init-build-env >/dev/null; \
	  bitbake-layers show-layers
	@test -f "$(YOCTO_BUILD_DIR)/conf/local.conf"
	@test -f "$(YOCTO_BUILD_DIR)/conf/bblayers.conf"

all: USERNS_CHECK $(YOCTO_BUILD_DIR)
	@cd "$(YOCTO_SOURCES_DIR)/poky"; \
		set -- "$(YOCTO_BUILD_DIR)"; \
		. ./oe-init-build-env >/dev/null; \
		bitbake "$(BITBAKE_TARGET)"

#------------------------------------
#
print_%:
	@echo '$*=$($*)'

help:

FORCE:;
# do not add recipes to this target

$(sort $(GENDIR)):
	@mkdir -p $(MKDIR_VERBOSE) $@

# ERROR: User namespaces are not usable by BitBake, possibly due to AppArmor.
# See https://discourse.ubuntu.com/t/ubuntu-24-04-lts-noble-numbat-release-notes/39890#unprivileged-user-namespace-restrictions for more information.
USERNS_CHECK: FORCE
	@if [ ! "$(APPARMOR_RESTRICT_USERNS)" = "0" ]; then \
	  echo "In case of the error \"User namespaces are not usable by BitBake ...\""; \
	  echo "Please execute \"echo 0 | sudo tee /proc/sys/kernel/apparmor_restrict_unprivileged_userns\""; \
	  false; \
	fi

#------------------------------------
# garage
#------------------------------------
# dkqqdiff1:
# 	cd layers/meta-openembedded \
# 	  && git add -N meta-oe/recipes-support/onig/onig/0003-fix-build-with-gcc-15-c23.patch \
# 	  && git diff -- \
# 	      meta-oe/recipes-support/onig/onig_6.9.9.bb \
# 	      meta-oe/recipes-devtools/jq/jq_1.7.1.bb \
# 	      meta-oe/recipes-support/onig/onig/0003-fix-build-with-gcc-15-c23.patch \
# 	      | tee $(PROJDIR)/0001-meta-openembedded.patch \
# 	  ; git reset meta-oe/recipes-support/onig/onig/0003-fix-build-with-gcc-15-c23.patch

# dkqqdiff2:
# 	cd layers/meta-tensorflow \
# 	  && git diff -- \
# 	      classes/bazel.bbclass \
# 	      | tee $(PROJDIR)/0001-meta-tensorflow.patch

# dkqqdiff: dkqqdiff1 dkqqdiff2

# USBQDL_CHECK: FORCE
# 	@if [ -z "$(LSUSB_QDL)" ]; then \
# 	  echo "No QDL device (05c6:9008)."; \
# 	  echo "For IQ-9075 EVK; power off, turn on SW2 (Mode) pin3, connect USB0 (ADB), power on"; \
# 	  false; \
# 	fi

#------------------------------------
#------------------------------------
#------------------------------------
#------------------------------------
