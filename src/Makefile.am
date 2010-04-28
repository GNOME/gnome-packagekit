## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(GCONF_CFLAGS)					\
	$(LIBNOTIFY_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	$(UNIQUE_CFLAGS)				\
	$(GTK_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(GNOME_MENUS_CFLAGS)				\
	-DI_KNOW_THE_UPOWER_API_IS_SUBJECT_TO_CHANGE	\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DG_UDEV_API_IS_SUBJECT_TO_CHANGE		\
	$(UPOWER_CFLAGS)				\
	$(CANBERRA_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DGPK_DATA=\"$(pkgdatadir)\"			\
	$(NULL)

bin_PROGRAMS =						\
	gpk-application					\
	gpk-repo					\
	gpk-prefs					\
	gpk-service-pack				\
	gpk-install-catalog				\
	gpk-install-local-file				\
	gpk-install-mime-type				\
	gpk-install-provide-file			\
	gpk-install-package-name			\
	gpk-update-icon					\
	gpk-update-viewer				\
	gpk-log						\
	gpk-backend-status				\
	gpk-dbus-service					\
	$(NULL)

noinst_LIBRARIES = libgpkshared.a
libgpkshared_a_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	egg-markdown.c					\
	egg-markdown.h					\
	egg-console-kit.c				\
	egg-console-kit.h				\
	egg-dbus-monitor.c				\
	egg-dbus-monitor.h				\
	gpk-enum.c					\
	gpk-enum.h					\
	gpk-x11.c					\
	gpk-x11.h					\
	gpk-marshal.c					\
	gpk-marshal.h					\
	gpk-session.c					\
	gpk-session.h					\
	gpk-desktop.c					\
	gpk-desktop.h					\
	gpk-animated-icon.c				\
	gpk-animated-icon.h				\
	gpk-dialog.c					\
	gpk-dialog.h					\
	gpk-vendor.c					\
	gpk-vendor.h					\
	gpk-language.c					\
	gpk-language.h					\
	gpk-modal-dialog.c				\
	gpk-modal-dialog.h				\
	gpk-helper-run.c				\
	gpk-helper-run.h				\
	gpk-helper-chooser.c				\
	gpk-helper-chooser.h				\
	gpk-gnome.c					\
	gpk-gnome.h					\
	gpk-common.c					\
	gpk-common.h					\
	gpk-task.c					\
	gpk-task.h					\
	gpk-error.c					\
	gpk-error.h					\
	$(NULL)

shared_LIBS =						\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(DBUS_LIBS)					\
	$(GCONF_LIBS)					\
	$(GTK_LIBS)					\
	$(GUDEV_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(PACKAGEKIT_LIBS)				\
	$(UNIQUE_LIBS)					\
	$(GNOME_MENUS_LIBS)				\
	$(UPOWER_LIBS)					\
	$(CANBERRA_LIBS)				\
	$(X11_LIBS)					\
	$(NULL)

gpk_install_provide_file_SOURCES =			\
	gpk-install-provide-file.c			\
	$(NULL)

gpk_install_provide_file_LDADD =			\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_install_catalog_SOURCES =				\
	gpk-install-catalog.c				\
	$(NULL)

gpk_install_catalog_LDADD =				\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_install_local_file_SOURCES =			\
	gpk-install-local-file.c			\
	$(NULL)

gpk_install_local_file_LDADD =				\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_install_mime_type_SOURCES =				\
	gpk-install-mime-type.c				\
	$(NULL)

gpk_install_mime_type_LDADD =				\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_install_package_name_SOURCES =			\
	gpk-install-package-name.c			\
	$(NULL)

gpk_install_package_name_LDADD =			\
	libgpkshared.a					\
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
	gpk-hardware.c					\
	gpk-hardware.h					\
	gpk-inhibit.c					\
	gpk-inhibit.h					\
	$(NULL)

gpk_update_icon_LDADD =					\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_dbus_service_SOURCES =				\
	gpk-dbus-service.c				\
	gpk-dbus.c					\
	gpk-dbus.h					\
	gpk-dbus-task.c					\
	gpk-dbus-task.h					\
	$(NULL)

gpk_dbus_service_LDADD =				\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_application_SOURCES =				\
	gpk-application-main.c				\
	gpk-application.c				\
	gpk-application.h				\
	gpk-cell-renderer-uri.c				\
	gpk-cell-renderer-uri.h				\
	$(NULL)

gpk_application_LDADD =					\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_service_pack_SOURCES =				\
	gpk-service-pack.c				\
	$(NULL)

gpk_service_pack_LDADD =				\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_prefs_SOURCES =					\
	gpk-prefs.c					\
	$(NULL)

gpk_prefs_LDADD =					\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_update_viewer_SOURCES =				\
	gpk-update-viewer-main.c			\
	gpk-update-viewer.c				\
	gpk-update-viewer.h				\
	gpk-cell-renderer-size.c			\
	gpk-cell-renderer-size.h			\
	gpk-cell-renderer-info.c			\
	gpk-cell-renderer-info.h			\
	gpk-cell-renderer-restart.c			\
	gpk-cell-renderer-restart.h			\
	$(NULL)

gpk_update_viewer_LDADD =				\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_repo_SOURCES =					\
	gpk-repo.c					\
	$(NULL)

gpk_repo_LDADD =					\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_log_SOURCES =					\
	gpk-log.c					\
	$(NULL)

gpk_log_LDADD =						\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_backend_status_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	gpk-backend-status.c				\
	$(NULL)

gpk_backend_status_LDADD =				\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

BUILT_SOURCES = 					\
	gpk-marshal.c					\
	gpk-marshal.h					\
	org.freedesktop.PackageKit.h			\
	$(NULL)

gpk-marshal.c: gpk-marshal.list
	echo "#include \"gpk-marshal.h\"" > $@ && \
	glib-genmarshal $< --prefix=gpk_marshal --body >> $@

gpk-marshal.h: gpk-marshal.list
	glib-genmarshal $< --prefix=gpk_marshal --header > $@

org.freedesktop.PackageKit.h: org.freedesktop.PackageKit.xml
	$(LIBTOOL) --mode=execute dbus-binding-tool	\
		--prefix=gpk_dbus			\
		--mode=glib-server			\
		--output=org.freedesktop.PackageKit.h	\
		$(srcdir)/org.freedesktop.PackageKit.xml

if EGG_BUILD_TESTS

check_PROGRAMS =					\
	gpk-self-test

noinst_PROGRAMS =					\
	gpk-self-test

gpk_self_test_SOURCES =					\
	gpk-self-test.c					\
	egg-test.h					\
	egg-test.c					\
	egg-string.c					\
	egg-string.h					\
	egg-markdown.c					\
	egg-markdown.h					\
	egg-debug.c					\
	egg-debug.h					\
	gpk-enum.c					\
	gpk-enum.h					\
	gpk-common.c					\
	gpk-common.h					\
	gpk-error.c					\
	gpk-error.h					\
	gpk-language.c					\
	gpk-language.h					\
	gpk-modal-dialog.c				\
	gpk-modal-dialog.h				\
	gpk-dbus.c					\
	gpk-dbus.h					\
	gpk-dbus-task.c					\
	gpk-dbus-task.h					\
	gpk-animated-icon.c				\
	gpk-animated-icon.h				\
	gpk-gnome.c					\
	gpk-gnome.h					\
	gpk-x11.c					\
	gpk-x11.h					\
	gpk-vendor.c					\
	gpk-vendor.h					\
	gpk-desktop.c					\
	gpk-desktop.h					\
	gpk-task.c					\
	gpk-task.h					\
	gpk-helper-run.c				\
	gpk-helper-run.h				\
	gpk-helper-chooser.c				\
	gpk-helper-chooser.h				\
	gpk-marshal.c					\
	gpk-marshal.h					\
	gpk-dialog.c					\
	gpk-dialog.h					\
	$(NULL)

gpk_self_test_LDADD =					\
	$(shared_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(NULL)

gpk_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = gpk-self-test
endif

EXTRA_DIST =						\
	gpk-marshal.list				\
	org.freedesktop.PackageKit.xml			\
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

