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
	-I$(top_srcdir)/libpackagekit-gnome		\
	-I$(top_srcdir)/libgbus				\
	$(NULL)

GPK_LIBS =						\
	$(top_builddir)/libpackagekit-gnome/libpackagekit-gnome.la	\
	$(NULL)

GBUS_LIBS =						\
	$(top_builddir)/libgbus/libgbus.la		\
	$(NULL)

bin_PROGRAMS =						\
	gpk-application					\
	gpk-repo					\
	gpk-prefs					\
	gpk-install-local-file				\
	gpk-install-provide-file			\
	gpk-install-package-name			\
	gpk-update-icon					\
	gpk-update-viewer				\
	gpk-log						\
	gpk-backend-status				\
	$(NULL)

gpk_install_provide_file_SOURCES =			\
	gpk-install-provide-file.c			\
	$(NULL)

gpk_install_provide_file_LDADD =			\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(GCONF_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(GPK_LIBS)					\
	$(NULL)

gpk_install_local_file_SOURCES =			\
	gpk-install-local-file.c			\
	$(NULL)

gpk_install_local_file_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(GCONF_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(GPK_LIBS)					\
	$(NULL)

gpk_install_package_name_SOURCES =			\
	gpk-install-package-name.c			\
	$(NULL)

gpk_install_package_name_LDADD =			\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(GCONF_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(GPK_LIBS)					\
	$(NULL)

gpk_update_icon_SOURCES =				\
	gpk-update-icon.c				\
	gpk-notify.c					\
	gpk-notify.h					\
	gpk-auto-refresh.c				\
	gpk-auto-refresh.h				\
	gpk-watch.c					\
	gpk-watch.h					\
	gpk-firmware.c					\
	gpk-firmware.h					\
	gpk-inhibit.c					\
	gpk-inhibit.h					\
	gpk-smart-icon.c				\
	gpk-smart-icon.h				\
	gpk-marshal.c					\
	gpk-marshal.h					\
	gpk-consolekit.c				\
	gpk-consolekit.h				\
	gpk-progress.c					\
	gpk-progress.h					\
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
	$(GPK_LIBS)					\
	$(NULL)

gpk_application_SOURCES =				\
	gpk-application-main.c				\
	gpk-application.c				\
	gpk-application.h				\
	gpk-statusbar.c					\
	gpk-statusbar.h					\
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
	$(GPK_LIBS)					\
	$(NULL)

gpk_prefs_SOURCES =					\
	gpk-prefs.c					\
	$(NULL)

gpk_prefs_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GCONF_LIBS)					\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(GPK_LIBS)					\
	$(NULL)

gpk_update_viewer_SOURCES =				\
	gpk-update-viewer.c				\
	gpk-statusbar.c					\
	gpk-statusbar.h					\
	gpk-consolekit.c				\
	gpk-consolekit.h				\
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
	$(GPK_LIBS)					\
	$(NULL)

gpk_repo_SOURCES =					\
	gpk-repo.c					\
	gpk-statusbar.c					\
	gpk-statusbar.h					\
	$(NULL)

gpk_repo_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GCONF_LIBS)					\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(GPK_LIBS)					\
	$(NULL)

gpk_log_SOURCES =					\
	gpk-log.c					\
	$(NULL)

gpk_log_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(GPK_LIBS)					\
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

BUILT_SOURCES = 					\
	gpk-marshal.c					\
	gpk-marshal.h					\
	gpk-interface.h					\
	$(NULL)

gpk-marshal.c: gpk-marshal.list
	echo "#include \"gpk-marshal.h\"" > $@ && \
	glib-genmarshal $< --prefix=gpk_marshal --body >> $@

gpk-marshal.h: gpk-marshal.list
	glib-genmarshal $< --prefix=gpk_marshal --header > $@

gpk-interface.h: gpk-interface.xml
	$(LIBTOOL) --mode=execute dbus-binding-tool	\
		--prefix=gpk_dbus			\
		--mode=glib-server			\
		--output=gpk-interface.h		\
		$(srcdir)/gpk-interface.xml

EXTRA_DIST =						\
	gpk-marshal.list				\
	gpk-interface.xml				\
	$(NULL)

clean-local:
	rm -f *~
	rm -f gpk-marshal.c gpk-marshal.h

CLEANFILES = *~ $(BUILT_SOURCES)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

