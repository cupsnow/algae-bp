#------------------------------------
#
PWD:=$(abspath .)
PROJDIR?=$(PWD)
BUILDDIR?=$(PROJDIR)/build
DESTDIR?=$(PROJDIR)/destdir
COMMA:=,
EMPTY:=#
SPACE:=$(EMPTY) $(EMPTY)
TAB:=$(EMPTY)	$(EMPTY)
define NEWLINE


endef
UPPERCASECHARACTERS=A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
LOWERCASECHARACTERS=a b c d e f g h i j k l m n o p q r s t u v w x y z

# $(info proj.mk ... MAKECMDGOALS: $(MAKECMDGOALS), PWD: $(PWD) \
#   $(EMPTY) PROJDIR: $(PROJDIR), PLATFORM: $(PLATFORM))

#$(foreach i,else-if,$(if $(filter $i,$(.FEATURES)),,$(warning '$i' not support, might ignore)))

ifeq ("$(or $(MSYSTEM),$(OS))","Windows_NT")
BUILDER_PLATFORM=windows
BUILDER_SHEXT=.bat
else
BUILDER_PLATFORM=unix
BUILDER_SHEXT=.sh
endif

#------------------------------------
#
C++ =$(CROSS_COMPILE)g++
CC=$(CROSS_COMPILE)gcc
AR=$(CROSS_COMPILE)ar
LD=$(CROSS_COMPILE)ld
OBJDUMP=$(CROSS_COMPILE)objdump
OBJCOPY=$(CROSS_COMPILE)objcopy
NM=$(CROSS_COMPILE)nm
SIZE=$(CROSS_COMPILE)size
STRIP=$(CROSS_COMPILE)strip
READELF=$(CROSS_COMPILE)readelf
RANLIB=$(CROSS_COMPILE)ranlib
MKDIR=mkdir -p
RMTREE=rm -rf

DEP=$(1).d
DEPFLAGS=-MMD -MT $(or $(1),$@) -MF $(call DEP,$(or $(1),$@)) -MP
DEPFLAGS=-MMD -MT $(@) -MF $(@).d -MP

#------------------------------------
#
CMD_SED_DEFNUM=sed -n "s/\\s*\#define\\s*$(1)\\s*\\(\\d*\\)/\\1/p"
CMD_SED_KEYVAL1=sed -n "s/\\s*$(1)\\s*=\\s*\\(.*\\)/\\1/p"

#------------------------------------
#
CMD_GIT_VER=git describe --always --tags

#------------------------------------
# $(call UNIQ,b b a a) # -> b a
#
UNIQ=$(if $1,$(strip $(firstword $1) $(call UNIQ,$(filter-out $(firstword $1),$1))))

#------------------------------------
# $(info AaBbccXXDF TOLOWER: $(call TOLOWER,AaBbccXXDF))
# $(info AaBbccXXDF TOUPPER: $(call TOUPPER,AaBbccXXDF))
#
MAPTO=$(subst $(firstword $1),$(firstword $2),$(if $(firstword $1),$(call MAPTO,$(filter-out $(firstword $1),$1),$(filter-out $(firstword $2),$2),$3),$3))
TOLOWER=$(call MAPTO,$(UPPERCASECHARACTERS),$(LOWERCASECHARACTERS),$1)
TOUPPER=$(call MAPTO,$(LOWERCASECHARACTERS),$(UPPERCASECHARACTERS),$1)

#------------------------------------
# EXTRA_PATH+=$(TOOLCHAIN_PATH:%=%/bin) $(TEST26DIR:%=%/tool/bin)
# export PATH:=$(call ENVPATH,$(EXTRA_PATH) $(PATH))
#
ENVPATH=$(subst $(SPACE),:,$(call UNIQ,$(subst :,$(SPACE),$(strip $1))))

#------------------------------------
# example: $(eval $(call GENDESTPKG,gmp))
# input 1 args
# 1. name
#
define GENDESTPKG
$(1)_destpkg $$($(1)_BUILDDIR)-destpkg.tar.xz:
	$$(RMTREE) $$($(1)_BUILDDIR)-destpkg
	$$(MAKE) DESTDIR=$$($(1)_BUILDDIR)-destpkg $(1)_install
	cd $$(dir $$($(1)_BUILDDIR)-destpkg) && \
	  tar -Jcvf $$($(1)_BUILDDIR)-destpkg.tar.xz \
	      $$(notdir $$($(1)_BUILDDIR)-destpkg)
	$$(RMTREE) $$($(1)_BUILDDIR)-destpkg

$(1)_destpkg_install: DESTDIR?=$$(SYSROOT)
$(1)_destpkg_install: | $$($(1)_BUILDDIR)-destpkg.tar.xz
	[ -d "$$(DESTDIR)" ] || $$(MKDIR) $$(DESTDIR)
	tar -Jxvf $$($(1)_BUILDDIR)-destpkg.tar.xz -C $$(DESTDIR) \
	    --strip-components=1

$(1)_destdep_install: $$(foreach iter,$$($(1)_DEP),$$(iter)_destdep_install)
	$$(MAKE) $(1)_destpkg_install
endef

#------------------------------------
# Linux kernel device tree source containing preprocessor macro
#
# $(call CPPDTS,at91-sama5d27_som1_ek.dtb.d1) \
#   $(addprefix -I,$(DTC_INCDIR)) \
#   -o at91-sama5d27_som1_ek.dtb.dts at91-sama5d27_som1_ek.dts
# $(call DTC2,at91-sama5d27_som1_ek.dtb.d2) \
#   $(addprefix -i,$(DTC_INCDIR)) \
#   -o at91-sama5d27_som1_ek.dtb at91-sama5d27_som1_ek.dtb.dts
# cat at91-sama5d27_som1_ek.dtb.d1 at91-sama5d27_som1_ek.dtb.d2 \
#   > at91-sama5d27_som1_ek.dtb.d
#
CMD_CPPDTS=gcc -E $(1:%=-Wp,-MMD,%) -nostdinc -undef -D__DTS__ \
  -x assembler-with-cpp
DTC_LINUX_WNO=-Wno-interrupt_provider -Wno-unit_address_vs_reg \
  -Wno-unit_address_format -Wno-avoid_unnecessary_addr_size -Wno-alias_paths \
  -Wno-graph_child_address -Wno-simple_bus_reg -Wno-unique_unit_address \
  -Wno-pci_device_reg
CMD_DTC2=dtc -O dtb -I dts -b 0 -@ $(DTC_LINUX_WNO) $(1:%=-d %)

#------------------------------------
# $(eval $(call DECL_TOOLCHAIN_GCC,$(HOME)/07_sw/gcc-aarch64-none-linux-gnu))
# $(eval $(call DECL_TOOLCHAIN_GCC,$(HOME)/07_sw/or1k-linux-musl,OR1K))
# EXTRA_PATH+=$(TOOLCHAIN_PATH:%=%/bin) $(OR1K_TOOLCHAIN_PATH:%=%/bin)
#
define DECL_TOOLCHAIN_GCC
$(2:%=%_)TOOLCHAIN_PATH=$1
$(2:%=%_)TOOLCHAIN_SYSROOT=$$(abspath $$(shell $$($(2:%=%_)TOOLCHAIN_PATH)/bin/*-gcc -print-sysroot))
$(2:%=%_)TOOLCHAIN_TARGET=$$(shell $$($(2:%=%_)TOOLCHAIN_PATH)/bin/*-gcc -dumpmachine)
$(2:%=%_)CROSS_COMPILE=$$($(2:%=%_)TOOLCHAIN_TARGET)-
endef

#------------------------------------
# dtc_dist_install: DESTDIR=$(PROJDIR)/tool
# dtc_dist_install:
# 	echo "NO_PYTHON=1" > $(dtc_BUILDDIR)_stain
# 	$(call RUN_DIST_INSTALL1,dtc,$(dtc_BUILDDIR)_stain $(dtc_BUILDDIR)/Makefile)
#
# bb_dist_install: CONFIG_PREFIX=$(BUILD_SYSROOT)
# bb_dist_install:
# 	$(call RUN_DIST_INSTALL1,bb CONFIG_PREFIX,$(bb_BUILDDIR)_stain $(bb_BUILDDIR)/Makefile)
#
define RUN_DIST_PACK1
if ! md5sum -c "$($(firstword $(1))_BUILDDIR).md5sum"; then \
  $(MAKE) $(or $(word 2,$(1)),DESTDIR)=$($(firstword $(1))_BUILDDIR)_destdir \
      $(firstword $(1))_install && \
  tar -cvf $($(firstword $(1))_BUILDDIR).tar \
      -C $(dir $($(firstword $(1))_BUILDDIR)_destdir) \
      $(notdir $($(firstword $(1))_BUILDDIR)_destdir) && \
  md5sum $($(firstword $(1))_BUILDDIR).tar \
      $(wildcard $($(firstword $(1))_BUILDDIR)_footprint $(2)) \
      > $($(firstword $(1))_BUILDDIR).md5sum && \
  $(RM) $($(firstword $(1))_BUILDDIR)_destdir; \
fi
endef

define RUN_DIST_INSTALL1
$(call RUN_DIST_PACK1,$(1),$(2))
[ -d "$($(or $(word 2,$(1)),DESTDIR))" ] || $(MKDIR) $($(or $(word 2,$(1)),DESTDIR))
tar -xvf $($(firstword $(1))_BUILDDIR).tar --strip-components=1 \
    -C $($(or $(word 2,$(1)),DESTDIR))
endef

#------------------------------------
# $(call CP_TAR,$(DESTDIR),$(TOOLCHAIN_SYSROOT), \
#   --exclude="*/gconv" --exclude="*.a" --exclude="*.o" --exclude="*.la", \
#   lib lib64 usr/lib usr/lib64)
#
define CP_TAR
	[ -d $(1) ] || $(MKDIR) $(1)
	PAT1=`echo -n '$(2)' | sed -e "s/^\/*//" -e 's/[\/&.]/\\\&/g'` && \
		echo "PAT1: $$PAT1" && \
		for i in $(4); do \
	    [ -e $(2)/$$i ] || \
			{ echo "Skip unknown $(2)/$$i"; continue ;}; \
		{ tar -cv --show-transformed-names \
			--transform="s/$$PAT1//" $(3) \
			$(2)/$$i | tar -xv -C $(1) ;}; \
		done
endef

#------------------------------------
# $(eval $(call AC_BUILD2,$(AC) $(DIR) $(BUILDDIR)))
# make AC="sox \$(PKGDIR2)/sox" APP_ATTR=ub20 sox_defconfig
#
AC_BUILD3_NAME=$(word 1,$(1))
define AC_BUILD3_HEAD
$(call AC_BUILD3_NAME,$(1))_DIR=$$(firstword $$(wildcard $(word 2,$(1)) \
    $$(PROJDIR)/package/$(call AC_BUILD3_NAME,$(1))))
$(call AC_BUILD3_NAME,$(1))_BUILDDIR?=$(or $(word 3,$(1)), \
    $$(BUILDDIR)/$(call AC_BUILD3_NAME,$(1))-$$(APP_BUILD))
$(call AC_BUILD3_NAME,$(1))_MAKE=$$($(call AC_BUILD3_NAME,$(1))_MAKEENV_$$(APP_PLATFORM)) \
    $$(MAKE) DESTDIR=$$(DESTDIR) $$($(call AC_BUILD3_NAME,$(1))_MAKEPARAM_$$(APP_PLATFORM)) \
    -C $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)
# end of AC_BUILD3_HEAD
endef

define AC_BUILD3_DEFCONFIG
$(call AC_BUILD3_NAME,$(1))_defconfig $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)/Makefile:
	if [ -x $$($(call AC_BUILD3_NAME,$(1))_DIR)/configure ]; then \
	  true; \
	elif [ -x $$($(call AC_BUILD3_NAME,$(1))_DIR)/autogen.sh ]; then \
	  cd $$($(call AC_BUILD3_NAME,$(1))_DIR) && ./autogen.sh; \
	else \
	  cd $$($(call AC_BUILD3_NAME,$(1))_DIR) && autoreconf -fiv; \
	fi
	[ -d "$$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)" ] || \
	    $$(MKDIR) $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)
	cd $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR) && \
	  $$(or $$($(call AC_BUILD3_NAME,$(1))_CFGENV_$$(APP_PLATFORM)),$$(BUILD_ENV)) \
	      $$($(call AC_BUILD3_NAME,$(1))_DIR)/configure --host=`$$(CC) -dumpmachine` \
	      --prefix="" $$($(call AC_BUILD3_NAME,$(1))_CFGPARAM_$$(APP_PLATFORM)) \
	      CPPFLAGS="$$(addprefix -I,$$(BUILD_SYSROOT)/include) \
	      $$($(call AC_BUILD3_NAME,$(1))_CFGPARAM_CPPFLAGS_$$(APP_PLATFORM))" \
	      LDFLAGS="$$(addprefix -L,$$(BUILD_SYSROOT)/lib $$(BUILD_SYSROOT)/lib64) \
	      $$($(call AC_BUILD3_NAME,$(1))_CFGPARAM_LDFLAGS_$$(APP_PLATFORM))"
# end of AC_BUILD3_DEFCONFIG
endef

define AC_BUILD3_DIST_INSTALL
$$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)_footprint:

$(call AC_BUILD3_NAME,$(1))_dist_pack:
	$$(RM) $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)_footprint
	$(MAKE) $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)_footprint
	$$(call RUN_DIST_PACK1,$(call AC_BUILD3_NAME,$(1)),$$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)/Makefile $(2))

$(call AC_BUILD3_NAME,$(1))_dist_install: DESTDIR=$$(BUILD_SYSROOT)
$(call AC_BUILD3_NAME,$(1))_dist_install:
	$$(RM) $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)_footprint
	$(MAKE) $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)_footprint
	$$(call RUN_DIST_INSTALL1,$(call AC_BUILD3_NAME,$(1)),$$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)/Makefile $(2))
# end of AC_BUILD3_DIST_INSTALL
endef

define AC_BUILD3_DISTCLEAN
$(call AC_BUILD3_NAME,$(1))_distclean:
	$$(RM) $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)
	if [ -x $$($(call AC_BUILD3_NAME,$(1))_DIR)/distclean.sh ]; then \
	  $$($(call AC_BUILD3_NAME,$(1))_DIR)/distclean.sh; \
	fi
# end of AC_BUILD3_DISTCLEAN
endef

define AC_BUILD3_FOOT
$(call AC_BUILD3_NAME,$(1))_install: DESTDIR=$$(BUILD_SYSROOT)

$(call AC_BUILD3_NAME,$(1)): $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)/Makefile
	$$($(call AC_BUILD3_NAME,$(1))_MAKE) $$(BUILDPARALLEL:%=-j%)

$(call AC_BUILD3_NAME,$(1))_%: $$($(call AC_BUILD3_NAME,$(1))_BUILDDIR)/Makefile
	$$($(call AC_BUILD3_NAME,$(1))_MAKE) $$(BUILDPARALLEL:%=-j%) $$(@:$(call AC_BUILD3_NAME,$(1))_%=%)
# end of AC_BUILD3_FOOT
endef

define AC_BUILD2
$(call AC_BUILD3_HEAD,$(1))
$(call AC_BUILD3_DEFCONFIG,$(1))
$(call AC_BUILD3_DIST_INSTALL,$(1),$(2))
$(call AC_BUILD3_DISTCLEAN,$(1))
$(call AC_BUILD3_FOOT,$(1))
# end of AC_BUILD2
endef

#------------------------------------
# $(call BUILD2,<name>, <objgen>, <src>)
#
define BUILD2
$1+=$3
$(1)_OBJGEN+=$$(patsubst %,$$(BUILDDIR)/$(2:%=%/)%.o,$$(filter %.cpp %.c %.S,$$($1)))
$(1)_INOBJ+=$$(filter %.a %.o,$$($1))
$(1)_LDSCRIPT+=$$(filter %.ld,$$($1))
$(1)_LDFLAGS+=$$($(1)_LDSCRIPT:%=-T %)
$(or $(2),BUILD2_BUILDDIR)_OBJGEN+=$$($(1)_OBJGEN)

$$($(1)_APP) $$($(1)_LIB): BUILD2_CPPFLAGS+=$$($(1)_CPPFLAGS)
$$($(1)_APP) $$($(1)_LIB): BUILD2_CFLAGS+=$$($(1)_CFLAGS)
$$($(1)_APP) $$($(1)_LIB): BUILD2_CXXFLAGS+=$$($(1)_CXXFLAGS)
$$($(1)_APP) $$($(1)_LIB): BUILD2_CC?=$$(or $$($(1)_CC),$$(CC))
$$($(1)_APP) $$($(1)_LIB): BUILD2_C++ ?=$$(or $$($(1)_C++),$$(C++))
$$($(1)_APP): BUILD2_LDFLAGS+=$$($(1)_LDFLAGS)
$$($(1)_LIB): BUILD2_ARFLAGS?=$$(or $$($(1)_ARFLAGS),rcs)
$$($(1)_LIB): BUILD2_AR?=$$(or $$($(1)_AR),$$(AR))

$$($(1)_APP): $$($(1)_OBJGEN) | $$($(1)_INOBJ)
	$$(MKDIR) $$(dir $$@)
	$$(or $$($(1)_LD),$$(if $$(filter %.cpp,$$($1)),$$(BUILD2_C++),$$(BUILD2_CC))) \
	  -o $$@ -Wl,--start-group $$($(1)_OBJGEN) $$($(1)_INOBJ) -Wl,--end-group \
	  $$(BUILD2_CPPFLAGS) $$(if $$(filter %.cpp,$$($1)),$$(BUILD2_CXXFLAGS),$$(BUILD2_CFLAGS)) \
	  $$(BUILD2_LDFLAGS)

$$($(1)_LIB): $$($(1)_OBJGEN) | $$($(1)_INOBJ)
	$$(MKDIR) $$(dir $$@)
	$$(BUILD2_AR) $$(BUILD2_ARFLAGS) $$@ $$($(1)_OBJGEN)

$(1)_clean:
	$$(RM) $$($(1)_OBJGEN) $$(addsuffix $$(DEP),$$($(1)_OBJGEN))

-include $$(addsuffix $$(DEP),$$($(1)_OBJGEN))
endef

define BUILD2_OBJGEN
$$(sort $$($(or $(1),BUILD2_BUILDDIR)_OBJGEN)): $$(BUILDDIR)/$(or $(2:%=%/),$(1:%=%/))%.o: %
	$$(MKDIR) $$(dir $$@)
	$$(if $$(filter %.cpp,$$<),$$(BUILD2_C++),$$(BUILD2_CC)) \
	  -c -o $$@ $$< $$(BUILD2_CPPFLAGS) \
	  $$(if $$(filter %.cpp,$$<),$$(BUILD2_CXXFLAGS),$$(BUILD2_CFLAGS))
	$$(if $$(filter %.cpp,$$<),$$(BUILD2_C++),$$(BUILD2_CC)) \
	  -E -o $$(call DEP,$$@) $$(call DEPFLAGS,$$@) $$< $$(BUILD2_CPPFLAGS) \
	  $$(if $$(filter %.cpp,$$<),$$(BUILD2_CXXFLAGS),$$(BUILD2_CFLAGS))
endef

#------------------------------------
#
define GITPROJ_DIST
$(or $(1),dist): DISTNAME=$(or $(strip $(2)),$$(or $$(shell PATH=$$(PATH) && git describe),master-$$(shell date '+%s')))
$(or $(1),dist):
	$$(RM) $$(BUILDDIR)/$$(DISTNAME)
	$$(MKDIR) $$(BUILDDIR)
	git clone --recurse-submodules ` ( cd $$(PROJDIR) && git remote get-url origin ) ` $$(BUILDDIR)/$$(DISTNAME)
	cd $$(BUILDDIR)/$$(DISTNAME) && \
	  git submodule foreach " ( rm -rf .git .gitmodules .gitignore ) "; rm -rf .git .gitmodules .gitignore playground
	$$(MKDIR) $$(DESTDIR)
	tar -Jcvf $$(DESTDIR)/$$(DISTNAME).tar.xz -C $$(BUILDDIR) $$(DISTNAME)
	$$(RM) $$(BUILDDIR)/$$(DISTNAME)
endef

#------------------------------------
#------------------------------------
#------------------------------------
#------------------------------------
#------------------------------------
#------------------------------------
