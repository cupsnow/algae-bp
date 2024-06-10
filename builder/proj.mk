#------------------------------------
#
PROJDIR?=$(abspath .)
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

# defer veriable CROSS_COMPILE 
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

DEPFLAGS=-MMD -MT $@ -MF ${@}.d -MP

Q=$(if $(V),,@)

#------------------------------------
#
CMD_SED_DEFNUM=sed -n "s/\\s*\#define\\s*$(1)\\s*\\(\\d*\\)/\\1/p"
CMD_SED_KEYVAL1=sed -n "s/\\s*$(1)\\s*=\\s*\\(.*\\)/\\1/p"

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

