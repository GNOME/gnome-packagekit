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
	$(UNIQUE_CFLAGS)				\
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
	-DEGG_LOG_FILE=\"\"				\
	$(NULL)

bin_PROGRAMS =						\
	gpk-application					\
	gpk-repo					\
	gpk-prefs					\
	gpk-install-catalog				\
	gpk-install-local-file				\
	gpk-install-mime-type				\
	gpk-install-provide-file			\
	gpk-install-package-name			\
	gpk-update-icon					\
	gpk-update-viewer				\
	gpk-log						\
	gpk-backend-status				\
	$(NULL)

shared_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	egg-unique.c					\
	egg-unique.h					\
	egg-dbus-monitor.c				\
	egg-dbus-monitor.h				\
	gpk-marshal.c					\
	gpk-marshal.h					\
	gpk-animated-icon.c				\
	gpk-animated-icon.h				\
	gpk-consolekit.c				\
	gpk-consolekit.h				\
	gpk-dialog.c					\
	gpk-dialog.h					\
	gpk-client.c					\
	gpk-client.h					\
	gpk-client-private.h				\
	gpk-client-eula.c				\
	gpk-client-eula.h				\
	gpk-client-resolve.c				\
	gpk-client-resolve.h				\
	gpk-client-depends.c				\
	gpk-client-depends.h				\
	gpk-client-requires.c				\
	gpk-client-requires.h				\
	gpk-client-signature.c				\
	gpk-client-signature.h				\
	gpk-client-untrusted.c				\
	gpk-client-untrusted.h				\
	gpk-client-chooser.c				\
	gpk-client-chooser.h				\
	gpk-client-run.c				\
	gpk-client-run.h				\
	gpk-smart-icon.c				\
	gpk-smart-icon.h				\
	gpk-gnome.c					\
	gpk-gnome.h					\
	gpk-common.c					\
	gpk-common.h					\
	gpk-error.c					\
	gpk-error.h					\
	$(NULL)

shared_LIBS =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(GCONF_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(PACKAGEKIT_LIBS)				\
	$(UNIQUE_LIBS)					\
	$(POLKIT_GNOME_LIBS)				\
	$(NULL)

gpk_install_provide_file_SOURCES =			\
	gpk-install-provide-file.c			\
	$(shared_SOURCES)				\
	$(NULL)

gpk_install_provide_file_LDADD =			\
	$(shared_LIBS)					\
	$(NULL)

gpk_install_catalog_SOURCES =				\
	gpk-install-catalog.c				\
	$(shared_SOURCES)				\
	$(NULL)

gpk_install_catalog_LDADD =				\
	$(shared_LIBS)					\
	$(NULL)

gpk_install_local_file_SOURCES =			\
	gpk-install-local-file.c			\
	$(shared_SOURCES)				\
	$(NULL)

gpk_install_local_file_LDADD =				\
	$(shared_LIBS)					\
	$(NULL)

gpk_install_mime_type_SOURCES =				\
	gpk-install-mime-type.c				\
	$(shared_SOURCES)				\
	$(NULL)

gpk_install_mime_type_LDADD =				\
	$(shared_LIBS)					\
	$(NULL)

gpk_install_package_name_SOURCES =			\
	gpk-install-package-name.c			\
	$(shared_SOURCES)				\
	$(NULL)

gpk_install_package_name_LDADD =			\
	$(shared_LIBS)					\
	$(NULL)

gpk_update_icon_SOURCES =				\
	gpk-update-icon.c				\
	gpk-check-update.c				\
	gpk-check-update.h				\
	gpk-auto-refresh.c				\
	gpk-auto-refresh.h				\
	gpk-watch.c					\
	gpk-watch.h					\
	gpk-firmware.c					\
	gpk-firmware.h					\
	gpk-inhibit.c					\
	gpk-inhibit.h					\
	gpk-dbus.c					\
	gpk-dbus.h					\
	$(shared_SOURCES)				\
	$(NULL)

gpk_update_icon_LDADD =					\
	$(shared_LIBS)					\
	$(NULL)

gpk_application_SOURCES =				\
	gpk-application-main.c				\
	gpk-application.c				\
	gpk-application.h				\
	gpk-application-state.c				\
	gpk-application-state.h				\
	gpk-cell-renderer-uri.c				\
	gpk-cell-renderer-uri.h				\
	$(shared_SOURCES)				\
	$(NULL)

gpk_application_LDADD =					\
	$(shared_LIBS)					\
	$(LIBSEXY_LIBS)					\
	$(NULL)

gpk_prefs_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	egg-unique.c					\
	egg-unique.h					\
	gpk-prefs.c					\
	gpk-gnome.c					\
	gpk-gnome.h					\
	$(NULL)

gpk_prefs_LDADD =					\
	$(shared_LIBS)					\
	$(NULL)

gpk_update_viewer_SOURCES =				\
	gpk-update-viewer.c				\
	gpk-cell-renderer-uri.c				\
	gpk-cell-renderer-uri.h				\
	$(shared_SOURCES)				\
	$(NULL)

gpk_update_viewer_LDADD =				\
	$(shared_LIBS)					\
	$(NULL)

gpk_repo_SOURCES =					\
	gpk-repo.c					\
	$(shared_SOURCES)				\
	$(NULL)

gpk_repo_LDADD =					\
	$(shared_LIBS)					\
	$(NULL)

gpk_log_SOURCES =					\
	gpk-log.c					\
	$(shared_SOURCES)				\
	$(NULL)

gpk_log_LDADD =						\
	$(shared_LIBS)					\
	$(NULL)

gpk_backend_status_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	gpk-backend-status.c				\
	$(NULL)

gpk_backend_status_LDADD =				\
	$(shared_LIBS)					\
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

check_PROGRAMS =					\
	gpk-self-test

noinst_PROGRAMS =					\
	gpk-self-test

gpk_self_test_SOURCES =					\
	gpk-self-test.c					\
	egg-test.h					\
	egg-test.c					\
	$(shared_SOURCES)				\
	$(NULL)

gpk_self_test_LDADD =					\
	$(shared_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(NULL)

gpk_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = gpk-self-test

EXTRA_DIST =						\
	gpk-marshal.list				\
	gpk-interface.xml				\
	$(NULL)

clean-local:
	rm -f *~
	rm -f gpk-marshal.c gpk-marshal.h
	rm -f *.out
	rm -f *.gcda
	rm -f *.gcno
	rm -f gcov.txt
	rm -f gprof.txt

CLEANFILES = *~ $(BUILT_SOURCES)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

