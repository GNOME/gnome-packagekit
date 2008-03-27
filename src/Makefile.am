## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(GCONF_CFLAGS)					\
	$(LIBGLADE_CFLAGS)				\
	$(LIBNOTIFY_CFLAGS)				\
	$(LIBSEXY_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	$(POLKIT_GNOME_CFLAGS)				\
	$(GTK_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-I$(top_srcdir)/libgbus				\
	-I$(top_srcdir)/libselftest			\
	$(NULL)

GBUS_LIBS =						\
	$(top_builddir)/libgbus/libgbus.la		\
	$(NULL)

SELFTEST_LIBS =						\
	$(top_builddir)/libselftest/libselftest.la	\
	$(NULL)

bin_PROGRAMS =						\
	pk-application					\
	pk-repo						\
	pk-prefs					\
	pk-install-file					\
	pk-install-package				\
	pk-update-icon					\
	pk-update-viewer				\
	pk-transaction-viewer				\
	pk-backend-status				\
	$(NULL)

pk_install_file_SOURCES =				\
	pk-install-file.c				\
	pk-progress.c					\
	pk-progress.h					\
	pk-common-gui.c					\
	pk-common-gui.h					\
	$(NULL)

pk_install_file_LDADD =					\
	$(GLIB_LIBS)					\
	$(GBUS_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(LIBGLADE_LIBS)				\
	$(NULL)

pk_install_package_SOURCES =				\
	pk-install-package.c				\
	pk-progress.c					\
	pk-progress.h					\
	pk-common-gui.c					\
	pk-common-gui.h					\
	$(NULL)

pk_install_package_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(LIBGLADE_LIBS)				\
	$(NULL)

pk_update_icon_SOURCES =				\
	pk-update-icon.c				\
	pk-notify.c					\
	pk-notify.h					\
	pk-auto-refresh.c				\
	pk-auto-refresh.h				\
	pk-watch.c					\
	pk-watch.h					\
	pk-inhibit.c					\
	pk-inhibit.h					\
	pk-smart-icon.c					\
	pk-smart-icon.h					\
	pk-progress.c					\
	pk-progress.h					\
	pk-common-gui.c					\
	pk-common-gui.h					\
	$(NULL)

pk_update_icon_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(GCONF_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

pk_application_SOURCES =				\
	pk-application-main.c				\
	pk-application.c				\
	pk-application.h				\
	pk-statusbar.c					\
	pk-statusbar.h					\
	pk-common-gui.c					\
	pk-common-gui.h					\
	$(NULL)

pk_application_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(GCONF_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(LIBSEXY_LIBS)					\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

pk_prefs_SOURCES =					\
	pk-prefs.c					\
	pk-common-gui.c					\
	pk-common-gui.h					\
	$(NULL)

pk_prefs_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GCONF_LIBS)					\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

pk_update_viewer_SOURCES =				\
	pk-update-viewer.c				\
	pk-statusbar.c					\
	pk-statusbar.h					\
	pk-common-gui.c					\
	pk-common-gui.h					\
	pk-cell-renderer-uri.c				\
	pk-cell-renderer-uri.h				\
	$(NULL)

pk_update_viewer_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

pk_repo_SOURCES =					\
	pk-repo.c					\
	pk-statusbar.c					\
	pk-statusbar.h					\
	pk-common-gui.c					\
	pk-common-gui.h					\
	$(NULL)

pk_repo_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

pk_transaction_viewer_SOURCES =				\
	pk-transaction-viewer.c				\
	pk-common-gui.c					\
	pk-common-gui.h					\
	$(NULL)

pk_transaction_viewer_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

pk_backend_status_SOURCES =				\
	pk-backend-status.c				\
	$(NULL)

pk_backend_status_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(NULL)

check_PROGRAMS =					\
	pk-self-test

noinst_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	pk-common-gui.h					\
	pk-common-gui.c					\
	pk-self-test.c					\
	$(NULL)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(DBUS_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(PK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

pk_self_test_CPPFLAGS=	\
	-DPK_BUILD_TESTS

BUILT_SOURCES = 					\
	pk-marshal.c					\
	pk-marshal.h					\
	$(NULL)

pk-marshal.c: pk-marshal.list
	echo "#include \"pk-marshal.h\"" > $@ && \
	glib-genmarshal $< --prefix=pk_marshal --body >> $@

pk-marshal.h: pk-marshal.list
	glib-genmarshal $< --prefix=pk_marshal --header > $@


EXTRA_DIST =						\
	pk-marshal.list					\
	$(NULL)

clean-local:
	rm -f *~
	rm -f pk-marshal.c pk-marshal.h

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

TESTS = pk-self-test

