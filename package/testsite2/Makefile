PROJDIR?=$(abspath .)
-include $(PROJDIR)/site.mk
-include $(PROJDIR)/builder/proj.mk

#------------------------------------
#
help: DESTDIR=$(BUILD_SYSROOT)
help:
	@echo "==== Start lighttpd with this command ===="
	@echo "DESTDIR=$(DESTDIR) LD_LIBRARY_PATH=$(DESTDIR)/lib $(DESTDIR)/sbin/lighttpd -m $(DESTDIR)/lib -f $(DESTDIR)/etc/lighttpd.conf -D"
	@echo "=============================================="

#------------------------------------
# use mod_setenv to set LD_LIBRARY_PATH for cgi
# DESTDIR=`pwd`/build/sysroot-ub20 LD_LIBRARY_PATH=`pwd`/build/sysroot-ub20/lib `pwd`/build/sysroot-ub20/sbin/lighttpd -m `pwd`/build/sysroot-ub20/lib -f `pwd`/build/sysroot-ub20/etc/lighttpd.conf -D
#
install: DESTDIR=$(BUILD_SYSROOT)
install:
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
