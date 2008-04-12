SUBDIRS =						\
	libselftest					\
	libgbus						\
	libpackagekit-gnome				\
	docs						\
	src						\
	po						\
	data						\
	help

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = packagekit-gnome.pc

EXTRA_DIST =						\
	MAINTAINERS					\
	intltool-extract.in				\
	intltool-merge.in				\
	intltool-update.in				\
	packagekit-gnome.pc.in				\
	omf.make					\
	xmldocs.make					\
	gnome-doc-utils.make

clean-local :
	rm -f *~

DISTCLEANFILES =					\
	intltool-extract				\
	intltool-merge					\
	intltool-update					\
	gnome-packagekit-*.tar.gz

DISTCHECK_CONFIGURE_FLAGS = --disable-schemas-install --disable-scrollkeeper --enable-gtk-doc

