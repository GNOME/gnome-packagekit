SUBDIRS =						\
	docs						\
	man						\
	src						\
	po						\
	data

ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

EXTRA_DIST =						\
	MAINTAINERS					\
	intltool-extract.in				\
	intltool-merge.in				\
	intltool-update.in				\
	omf.make

clean-local :
	rm -f *~

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-$(VERSION)-`date +"%Y%m%d"`

DISTCLEANFILES =					\
	intltool-extract				\
	intltool-merge					\
	intltool-update					\
	gnome-packagekit-*.tar.gz

DISTCHECK_CONFIGURE_FLAGS = --disable-scrollkeeper

distclean-local:
	if test $(srdcir) = .; then :; else \
		rm -f ChangeLog; \
	fi

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log 699c5554d899c5f3d38adb60bc825009ff9a63cd.. --stat -M -C --name-status  --date=short --no-color) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog
