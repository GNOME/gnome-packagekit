## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(GCONF_CFLAGS)					\
	$(LIBGLADE_CFLAGS)				\
	$(LIBNOTIFY_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	$(GTK_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-I$(top_srcdir)/libpackagekit			\
	$(NULL)

bin_PROGRAMS =						\
	pk-application					\
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
	pk-common.c					\
	pk-common.h					\
	$(NULL)

pk_install_file_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(LIBGLADE_LIBS)				\
	$(NULL)

pk_install_package_SOURCES =				\
	pk-install-package.c				\
	pk-progress.c					\
	pk-progress.h					\
	pk-common.c					\
	pk-common.h					\
	$(NULL)

pk_install_package_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(LIBGLADE_LIBS)				\
	$(NULL)

pk_update_icon_SOURCES =				\
	pk-update-icon.c				\
	pk-notify.c					\
	pk-notify.h					\
	pk-watch.c					\
	pk-watch.h					\
	pk-progress.c					\
	pk-progress.h					\
	pk-common.c					\
	pk-common.h					\
	$(NULL)

pk_update_icon_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(GCONF_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(PACKAGEKIT_LIBS)				\
	$(NULL)

pk_application_SOURCES =				\
	pk-application-main.c				\
	pk-application.c				\
	pk-application.h				\
	pk-common.c					\
	pk-common.h					\
	$(NULL)

pk_application_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(NULL)

pk_prefs_SOURCES =					\
	pk-prefs.c					\
	pk-common.c					\
	pk-common.h					\
	$(NULL)

pk_prefs_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GCONF_LIBS)					\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(NULL)

pk_update_viewer_SOURCES =				\
	pk-update-viewer.c				\
	pk-common.c					\
	pk-common.h					\
	$(NULL)

pk_update_viewer_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(NULL)

pk_transaction_viewer_SOURCES =				\
	pk-transaction-viewer.c				\
	pk-common.c					\
	pk-common.h					\
	$(NULL)

pk_transaction_viewer_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
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

clean-local:
	rm -f *~

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

