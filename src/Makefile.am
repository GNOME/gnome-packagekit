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
	gpk-application					\
	gpk-repo					\
	gpk-prefs					\
	gpk-install-file				\
	gpk-install-package				\
	gpk-update-icon					\
	gpk-update-viewer				\
	gpk-transaction-viewer				\
	gpk-backend-status				\
	$(NULL)

gpk_install_file_SOURCES =				\
	gpk-install-file.c				\
	gpk-progress.c					\
	gpk-progress.h					\
	gpk-common.c					\
	gpk-common.h					\
	$(NULL)

gpk_install_file_LDADD =				\
	$(GLIB_LIBS)					\
	$(GBUS_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(LIBGLADE_LIBS)				\
	$(NULL)

gpk_install_package_SOURCES =				\
	gpk-install-package.c				\
	gpk-progress.c					\
	gpk-progress.h					\
	gpk-common.c					\
	gpk-common.h					\
	$(NULL)

gpk_install_package_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(LIBGLADE_LIBS)				\
	$(NULL)

gpk_update_icon_SOURCES =				\
	gpk-update-icon.c				\
	gpk-notify.c					\
	gpk-notify.h					\
	gpk-auto-refresh.c				\
	gpk-auto-refresh.h				\
	gpk-watch.c					\
	gpk-watch.h					\
	gpk-inhibit.c					\
	gpk-inhibit.h					\
	gpk-smart-icon.c				\
	gpk-smart-icon.h				\
	gpk-marshal.c					\
	gpk-marshal.h					\
	gpk-progress.c					\
	gpk-progress.h					\
	gpk-common.c					\
	gpk-common.h					\
	$(NULL)

gpk_update_icon_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(GCONF_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

gpk_application_SOURCES =				\
	gpk-application-main.c				\
	gpk-application.c				\
	gpk-application.h				\
	gpk-statusbar.c					\
	gpk-statusbar.h					\
	gpk-common.c					\
	gpk-common.h					\
	$(NULL)

gpk_application_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(GCONF_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(LIBSEXY_LIBS)					\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

gpk_prefs_SOURCES =					\
	gpk-prefs.c					\
	gpk-common.c					\
	gpk-common.h					\
	$(NULL)

gpk_prefs_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GCONF_LIBS)					\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

gpk_update_viewer_SOURCES =				\
	gpk-update-viewer.c				\
	gpk-statusbar.c					\
	gpk-statusbar.h					\
	gpk-common.c					\
	gpk-common.h					\
	gpk-cell-renderer-uri.c				\
	gpk-cell-renderer-uri.h				\
	$(NULL)

gpk_update_viewer_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

gpk_repo_SOURCES =					\
	gpk-repo.c					\
	gpk-statusbar.c					\
	gpk-statusbar.h					\
	gpk-common.c					\
	gpk-common.h					\
	$(NULL)

gpk_repo_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

gpk_transaction_viewer_SOURCES =			\
	gpk-transaction-viewer.c			\
	gpk-common.c					\
	gpk-common.h					\
	$(NULL)

gpk_transaction_viewer_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

gpk_backend_status_SOURCES =				\
	gpk-backend-status.c				\
	$(NULL)

gpk_backend_status_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(NULL)

check_PROGRAMS =					\
	gpk-self-test

noinst_PROGRAMS =					\
	gpk-self-test

gpk_self_test_SOURCES =					\
	gpk-common.h					\
	gpk-common.c					\
	gpk-self-test.c					\
	$(NULL)

gpk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(DBUS_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(PK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

gpk_self_test_CPPFLAGS=	\
	-DPK_BUILD_TESTS

BUILT_SOURCES = 					\
	gpk-marshal.c					\
	gpk-marshal.h					\
	$(NULL)

gpk-marshal.c: gpk-marshal.list
	echo "#include \"gpk-marshal.h\"" > $@ && \
	glib-genmarshal $< --prefix=gpk_marshal --body >> $@

gpk-marshal.h: gpk-marshal.list
	glib-genmarshal $< --prefix=gpk_marshal --header > $@


EXTRA_DIST =						\
	gpk-marshal.list				\
	$(NULL)

clean-local:
	rm -f *~
	rm -f gpk-marshal.c gpk-marshal.h

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

TESTS = gpk-self-test

