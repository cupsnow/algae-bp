PROJDIR?=$(abspath .)
-include $(PROJDIR)/site.mk
-include $(PROJDIR:%=%/builder/)proj.mk


#------------------------------------
#
fcgi_CFLAGS+=-I$(BUILD_SYSROOT)/include
fcgi_LDFLAGS+=-L$(BUILD_SYSROOT)/lib64 -L$(BUILD_SYSROOT)/lib -l fcgi

#------------------------------------
#
help: DESTDIR=$(BUILD_SYSROOT)
help:
	@echo "==== Start lighttpd with this command ===="
	@echo "DESTDIR=$(DESTDIR) LD_LIBRARY_PATH=$(DESTDIR)/lib $(DESTDIR)/sbin/lighttpd -m $(DESTDIR)/lib -f $(DESTDIR)/etc/lighttpd.conf -D"
	@echo "=============================================="

#------------------------------------
#
testsite2_BUILDDIR=$(BUILDDIR)/testsite2
testsite2_SRCS=main.cpp
testsite2_OBJS=$(patsubst %,$(testsite2_BUILDDIR)/%.o,$(filter %.cpp,$(testsite2_SRCS)))
testsite2_OBJS+=$(patsubst %,$(testsite2_BUILDDIR)/%.o,$(filter %.c,$(testsite2_SRCS)))
testsite2_DEPS=$(testsite2_OBJS:%=%.d)

testsite2_CFLAGS+=$(fcgi_CFLAGS)
testsite2_CXXFLAGS+=$(fcgi_CFLAGS)
testsite2_LDFLAGS+=$(fcgi_LDFLAGS)

$(testsite2_BUILDDIR)/testsite2: $(testsite2_OBJS)
	$(if $(filter %.cpp,$(testsite2_SRCS)),g++,gcc) \
	  -o $@ $(testsite2_OBJS) -pthread $(LDFLAGS) \
	  $(testsite2_LDFLAGS)

$(testsite2_BUILDDIR)/%.c.o: %.c
	$(MKDIR) $(dir $@)
	gcc -c -o $@ $< $(testsite2_CFLAGS) -pthread $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS)

$(testsite2_BUILDDIR)/%.cpp.o: %.cpp
	$(MKDIR) $(dir $@)
	g++ -c -o $@ $< $(testsite2_CXXFLAGS) -pthread $(CXXFLAGS) $(CPPFLAGS) $(DEPFLAGS)

-include $(testsite2_DEPS)

#------------------------------------
# use mod_setenv to set LD_LIBRARY_PATH for cgi
# DESTDIR=`pwd`/build/sysroot-ub20 LD_LIBRARY_PATH=`pwd`/build/sysroot-ub20/lib `pwd`/build/sysroot-ub20/sbin/lighttpd -m `pwd`/build/sysroot-ub20/lib -f `pwd`/build/sysroot-ub20/etc/lighttpd.conf -D
#
install: DESTDIR=$(BUILD_SYSROOT)
install: $(testsite2_BUILDDIR)/testsite2
	$(MKDIR) $(DESTDIR)/etc $(DESTDIR)/var/www $(DESTDIR)/var/cgi-bin \
	    $(DESTDIR)/var/run $(DESTDIR)/media
	ln -sfnv $(CURDIR)/lighttpd.conf $(DESTDIR)/etc/
	for i in admin2.html \
	    ; do \
	  ln -sfnv $(CURDIR)/$$i $(DESTDIR)/var/www/; \
	done
	for i in admin_observer1.cgi \
	    ; do \
	  ln -sfnv $(CURDIR)/$$i $(DESTDIR)/var/cgi-bin/; \
	done
	$(MAKE) DESTDIR=$(DESTDIR) help
