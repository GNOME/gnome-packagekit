SUBDIRS =						\
	libselftest					\
	libgbus						\
	libunique					\
	docs						\
	man						\
	src						\
	po						\
	data						\
	help

EXTRA_DIST =						\
	MAINTAINERS					\
	intltool-extract.in				\
	intltool-merge.in				\
	intltool-update.in				\
	omf.make					\
	xmldocs.make					\
	gnome-doc-utils.make

clean-local :
	rm -f *~

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-$(VERSION)-`date +"%Y%m%d"`

DISTCLEANFILES =					\
	intltool-extract				\
	intltool-merge					\
	intltool-update					\
	gnome-packagekit-*.tar.gz

DISTCHECK_CONFIGURE_FLAGS = --disable-schemas-install --disable-scrollkeeper

