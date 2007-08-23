SUBDIRS =						\
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

DISTCLEANFILES =					\
	intltool-extract				\
	intltool-merge					\
	intltool-update					\
	packagekit-gnome-*.tar.gz

DISTCHECK_CONFIGURE_FLAGS = --disable-schemas-install --disable-scrollkeeper

