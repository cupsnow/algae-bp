ncursesw_DIR?=$(PKGDIR2)/ncurses
ncursesw_BUILDDIR?=$(BUILDDIR2)/ncursesw-$(APP_BUILD)
ncursesw_TINFODIR=/usr/share/terminfo
ncursesw_MAKE=$(MAKE) -C $(ncursesw_BUILDDIR)

# ncursesw_ACARGS_$(APP_PLATFORM)+=--without-debug

ncursesw_ACARGS_iq9+=

ncursesw_ACARGS_ub20+=--enable-pc-files --with-pkg-config-libdir=/lib/pkgconfig
ncursesw_ACARGS_bp+=--without-tests --without-manpages --disable-db-install
ncursesw_ACARGS_CPPFLAGS_iq9+=--sysroot=$(TOOLCHAIN_SYSROOT)
ncursesw_ACARGS_LDFLAGS_iq9+=--sysroot=$(TOOLCHAIN_SYSROOT)

ncursesw_MAKEENV_bp=LD_LIBRARY_PATH=$(PROJDIR)/tool/lib \
    TERMINFO=$(PROJDIR)/tool/$(ncursesw_TINFODIR)

GENDIR+=$(ncursesw_BUILDDIR)

# no strip to prevent not recoginize crosscompiled executable
ncursesw_defconfig $(ncursesw_BUILDDIR)/Makefile: | $(ncursesw_BUILDDIR)
	cd $(ncursesw_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(ncursesw_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= --with-termlib --with-ticlib \
	      --with-shared --enable-widec --disable-stripping --without-ada \
		  --with-default-terminfo-dir=$(ncursesw_TINFODIR) \
	      CFLAGS="-fPIC $(ncursesw_ACARGS_CFLAGS_$(APP_PLATFORM))" \
	      CPPFLAGS="$(ncursesw_ACARGS_CPPFLAGS_$(APP_PLATFORM))" \
	      LDFLAGS="$(ncursesw_ACARGS_LDFLAGS_$(APP_PLATFORM))" \
	      $(ncursesw_ACARGS_$(APP_PLATFORM))

ncursesw_host_install: DESTDIR=$(PROJDIR)/tool
ncursesw_host_install:
	$(MAKE) APP_PLATFORM=ub20 $(PARALLEL_BUILD) ncursesw
	$(MAKE) APP_PLATFORM=ub20 DESTDIR=$(DESTDIR) ncursesw_install
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	echo "INPUT(-lncursesw)" > $(DESTDIR)/lib/libcurses.so;
	ln -sfn libncurses.a $(DESTDIR)/lib/libcurses.a
	for i in ncurses form panel menu tinfo; do \
	  if [ -e $(DESTDIR)/lib/lib$${i}w.so ]; then \
	    echo "INPUT(-l$${i}w)" > $(DESTDIR)/lib/lib$${i}.so; \
	  fi; \
	  if [ -e $(DESTDIR)/lib/lib$${i}w.a ]; then \
	    ln -sfn lib$${i}w.a $(DESTDIR)/lib/lib$${i}.a; \
	  fi; \
	done


ifeq ($(strip $(filter ub20,$(APP_PLATFORM))),)
ncursesw_install: | $(PROJDIR)/tool/bin/tic
endif

ncursesw_install: DESTDIR=$(BUILD_SYSROOT)
ncursesw_install: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(PARALLEL_BUILD)
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	LD_LIBRARY_PATH=$(PROJDIR)/tool/lib \
        TERMINFO=$(PROJDIR)/tool/$(ncursesw_TINFODIR) \
	    $(ncursesw_MAKE) DESTDIR=$(DESTDIR) install
	echo "INPUT(-lncursesw)" > $(DESTDIR)/lib/libcurses.so;
	for i in ncurses form panel menu tinfo; do \
	  if [ -e $(DESTDIR)/lib/lib$${i}w.so ]; then \
	    echo "INPUT(-l$${i}w)" > $(DESTDIR)/lib/lib$${i}.so; \
	  fi; \
	  if [ -e $(DESTDIR)/lib/lib$${i}w.a ]; then \
	    ln -sfn lib$${i}w.a $(DESTDIR)/lib/lib$${i}.a; \
	  fi; \
	done

$(eval $(call DEF_DESTDEP,ncursesw))

ncursesw: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(PARALLEL_BUILD)

ncursesw_%: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(PARALLEL_BUILD) $(@:ncursesw_%=%)

$(addprefix $(PROJDIR)/tool/bin/,tic infocmp) ncursesw_host:
	$(MAKE) APP_PLATFORM=ub20  DESTDIR=$(PROJDIR)/tool ncursesw_destdep_install

CMD_TIC=LD_LIBRARY_PATH=$(PROJDIR)/tool/lib \
    TERMINFO=$(PROJDIR)/tool/$(ncursesw_TINFODIR) \
	$(PROJDIR)/tool/bin/tic
CMD_INFOCMP=LD_LIBRARY_PATH=$(PROJDIR)/tool/lib \
    TERMINFO=$(PROJDIR)/tool/$(ncursesw_TINFODIR) \
	$(PROJDIR)/tool/bin/infocmp

terminfo_BUILDDIR?=$(BUILDDIR)/terminfo

terminfo_install: DESTDIR=$(BUILD_SYSROOT)/usr/share
terminfo_install: | $(PROJDIR)/tool/bin/tic
	[ -d "$(DESTDIR)/terminfo" ] || $(MKDIR) $(DESTDIR)/terminfo
	for i in vt100 linux xterm xterm-256color screen screen-256color tmux-256color; do \
	  $(CMD_INFOCMP) -x $$i \
	    | $(CMD_TIC) -x -o $(DESTDIR)/terminfo -; \
	done

$(eval $(call DEF_DESTDEP,terminfo))
