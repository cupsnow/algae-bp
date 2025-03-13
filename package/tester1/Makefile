#------------------------------------
#
-include $(PROJDIR:%=%/builder/)proj.mk

tester_DEPFLAGS=-MMD -MF $(@).d -MT $@
tester_CPPFLAGS+=-g -pthread -I$(BUILD_SYSROOT)/include
tester_LDFLAGS+=-pthread -L$(BUILD_SYSROOT)/lib -ladmin \
    -lcjson  -lssl -lcrypto -lgpiod \
    -lavformat -lavcodec -lswresample -lavutil \
    -lm

.DEFAULT_GOAL:=all

#------------------------------------
#
GENDIR+=$(BUILDDIR)

$(BUILDDIR)/%.c.o: %.c | $(BUILDDIR)
	$(CC) -c -o $@ $< $(tester_DEPFLAGS) $(tester_CPPFLAGS) $(tester_CFLAGS)

$(BUILDDIR)/%.cpp.o: %.cpp | $(BUILDDIR)
	$(C++) -c -o $@ $< $(tester_DEPFLAGS) $(tester_CPPFLAGS) $(tester_CXXFLAGS)

#------------------------------------
#
tester_net1_SRC=tester_net1.cpp
tester_net1_OBJ=$(patsubst %,$(BUILDDIR)/%.o,$(filter %.c %.cpp,$(tester_net1_SRC)))
tester_net1_DEP=$(tester_net1_OBJ:%=%.d)
tester_net1_BIN=tester_net1

$(BUILDDIR)/$(tester_net1_BIN): $(tester_net1_OBJ)
	$(if $(filter %.cpp,$(tester_net1_SRC)),$(C++),$(CC)) \
	    -o $(BUILDDIR)/$(tester_net1_BIN) $(filter %.o %.a,$^) \
	    $(tester_LDFLAGS)

ARTIFACT+=tester_net1

#------------------------------------
#
-include $(foreach iter,$(ARTIFACT),$($(iter)_DEP)))

all: $(addprefix $(BUILDDIR)/,$(foreach iter,$(ARTIFACT),$($(iter)_BIN)))

#------------------------------------
#

$(sort $(GENDIR)):
	mkdir -p $@
