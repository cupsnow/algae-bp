#------------------------------------
#
PROJDIR?=$(PWD)
include $(PROJDIR)/builder/proj.mk

CPPFLAGS+=
CFLAGS+=
CXXFLAGS+=
LDFLAGS+=

GENDIR:=

#------------------------------------
#
dummy_DIR=.
dummy_BUILDDIR=$(BUILDDIR)/dummy1
dummy_BIN=dummy1
dummy_SRCS=$(wildcard $(dummy_DIR)/*.c $(dummy_DIR)/*.cpp)
dummy_OBJS=$(dummy_SRCS:$(dummy_DIR)/%=$(dummy_BUILDDIR)/%.o)
dummy_DEPS=$(dummy_OBJS:%=%.d)
#dummy_LDFLAGS=-static $(LDFLAGS)
 
dummy1: $(dummy_BUILDDIR)/$(dummy_BIN)

$(dummy_BUILDDIR)/$(dummy_BIN): $(dummy_OBJS) | $(dummy_BUILDDIR)
	$(C++) -o $(dummy_BUILDDIR)/$(dummy_BIN) $(filter %.o %.a,$^) \
	    $(or $(dummy_LDFLAGS),$(LDFLAGS))

$(dummy_BUILDDIR)/%.c.o: $(dummy_DIR)/%.c | $(dummy_BUILDDIR)
	$(CC) -c -o $@ $< $(DEPFLAGS) $(or $(dummy_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(dummy_CFLAGS),$(CFLAGS))

$(dummy_BUILDDIR)/%.cpp.o: $(dummy_DIR)/%.cpp | $(dummy_BUILDDIR)
	$(C++) -c -o $@ $< $(DEPFLAGS) $(or $(dummy_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(dummy_CXXFLAGS),$(CXXFLAGS))

GENDIR+=$(dummy_BUILDDIR)

#------------------------------------
#
$(sort $(GENDIR)):
	$(MKDIR) $@

#------------------------------------
#------------------------------------
#------------------------------------
#