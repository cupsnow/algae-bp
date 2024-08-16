#------------------------------------
#
PROJDIR?=$(PWD)
include $(PROJDIR)/builder/proj.mk

CPPFLAGS+=
CFLAGS+=
CXXFLAGS+=
LDFLAGS+=

GENDIR:=

ARTIFACT:=
ARTIFACT_INSTALL:=

.DEFAULT_GOAL:=artifact

#------------------------------------
#
dummy_DIR=.
dummy_BUILDDIR=$(BUILDDIR)/dummy1
dummy_BIN=dummy1
dummy_SRCS=$(wildcard $(addprefix $(dummy_DIR)/, \
  util.c main.cpp))
dummy_OBJS=$(dummy_SRCS:$(dummy_DIR)/%=$(dummy_BUILDDIR)/%.o)
dummy_DEPS=$(dummy_OBJS:%=%.d)
#dummy_LDFLAGS=-static $(LDFLAGS)
 
dummy: $(dummy_BUILDDIR)/$(dummy_BIN)

$(dummy_BUILDDIR)/$(dummy_BIN): $(dummy_OBJS) | $(dummy_BUILDDIR)
	$(C++) -o $(dummy_BUILDDIR)/$(dummy_BIN) $(filter %.o %.a,$^) \
	    $(or $(dummy_LDFLAGS),$(LDFLAGS))

$(dummy_BUILDDIR)/%.c.o: $(dummy_DIR)/%.c | $(dummy_BUILDDIR)
	$(CC) -c -o $@ $< $(DEPFLAGS) $(or $(dummy_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(dummy_CFLAGS),$(CFLAGS))

$(dummy_BUILDDIR)/%.cpp.o: $(dummy_DIR)/%.cpp | $(dummy_BUILDDIR)
	$(C++) -c -o $@ $< $(DEPFLAGS) $(or $(dummy_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(dummy_CXXFLAGS),$(CXXFLAGS))

ARTIFACT+=dummy

GENDIR+=$(dummy_BUILDDIR)

#------------------------------------
#
tester_syslog_DIR=.
tester_syslog_BUILDDIR=$(BUILDDIR)/dummy1
tester_syslog_BIN=tester_syslog
tester_syslog_SRCS=$(wildcard $(addprefix $(tester_syslog_DIR)/, \
  tester_main.c tester_syslog.cpp util.c))
tester_syslog_OBJS=$(tester_syslog_SRCS:$(tester_syslog_DIR)/%=$(tester_syslog_BUILDDIR)/%.o)
tester_syslog_DEPS=$(tester_syslog_OBJS:%=%.d)
#tester_syslog_LDFLAGS=-static $(LDFLAGS)
 
.PHONY: tester_syslog
tester_syslog: $(tester_syslog_BUILDDIR)/$(tester_syslog_BIN)

$(tester_syslog_BUILDDIR)/$(tester_syslog_BIN): $(tester_syslog_OBJS) | $(tester_syslog_BUILDDIR)
	$(C++) -o $(tester_syslog_BUILDDIR)/$(tester_syslog_BIN) $(filter %.o %.a,$^) \
	    $(or $(tester_syslog_LDFLAGS),$(LDFLAGS))

$(tester_syslog_BUILDDIR)/%.c.o: $(tester_syslog_DIR)/%.c | $(tester_syslog_BUILDDIR)
	$(CC) -c -o $@ $< $(DEPFLAGS) $(or $(tester_syslog_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(tester_syslog_CFLAGS),$(CFLAGS))

$(tester_syslog_BUILDDIR)/%.cpp.o: $(tester_syslog_DIR)/%.cpp | $(tester_syslog_BUILDDIR)
	$(C++) -c -o $@ $< $(DEPFLAGS) $(or $(tester_syslog_CPPFLAGS),$(CPPFLAGS)) \
	    $(or $(tester_syslog_CXXFLAGS),$(CXXFLAGS))

ARTIFACT+=tester_syslog

GENDIR+=$(tester_syslog_BUILDDIR)

#------------------------------------
#
artifact: $(ARTIFACT)

-include $(sort $(foreach iter,$(ARTIFACT),$(${iter}_DEPS)))

#------------------------------------
#
$(sort $(GENDIR)):
	$(MKDIR) $@

#------------------------------------
#------------------------------------
#------------------------------------
#