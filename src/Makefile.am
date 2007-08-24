## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
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
	pk-update-icon					\
	$(NULL)

pk_update_icon_SOURCES =				\
	pk-update-icon.c				\
	pk-notify.c					\
	pk-notify.h					\
	pk-common.c					\
	pk-common.h					\
	$(NULL)

pk_update_icon_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(NULL)

pk_application_SOURCES =				\
	pk-application-main.c				\
	pk-application.c				\
	pk-application.h				\
	$(NULL)

pk_application_LDADD =					\
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

