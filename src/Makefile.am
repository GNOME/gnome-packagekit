## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(NOTIFY_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	$(GUDEV_CFLAGS)					\
	$(SYSTEMD_CFLAGS)				\
	$(GNOME_MENUS_CFLAGS)				\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DG_UDEV_API_IS_SUBJECT_TO_CHANGE		\
	$(CANBERRA_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DGPK_DATA=\"$(pkgdatadir)\"			\
	-DG_LOG_DOMAIN=\"GnomePackageKit\"		\
	$(NULL)

bin_PROGRAMS =						\
	gpk-application					\
	gpk-prefs					\
	gpk-install-local-file				\
	gpk-update-viewer				\
	gpk-log						\
	gpk-dbus-service				\
	$(NULL)

noinst_LIBRARIES = libgpkshared.a
libgpkshared_a_SOURCES =				\
	egg-string.c					\
	egg-string.h					\
	egg-markdown.c					\
	egg-markdown.h					\
	gpk-debug.c					\
	gpk-debug.h					\
	gpk-enum.c					\
	gpk-enum.h					\
	gpk-x11.c					\
	gpk-x11.h					\
	gpk-marshal.c					\
	gpk-marshal.h					\
	gpk-session.c					\
	gpk-session.h					\
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

if WITH_SYSTEMD
libgpkshared_a_SOURCES +=				\
	systemd-proxy.c					\
	systemd-proxy.h
endif

shared_LIBS =						\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(DBUS_LIBS)					\
	$(GTK_LIBS)					\
	$(GUDEV_LIBS)					\
	$(NOTIFY_LIBS)					\
	$(SYSTEMD_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(GNOME_MENUS_LIBS)				\
	$(CANBERRA_LIBS)				\
	$(X11_LIBS)					\
	$(NULL)

gpk_install_local_file_SOURCES =			\
	gpk-install-local-file.c			\
	$(NULL)

gpk_install_local_file_LDADD =				\
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
	gpk-application.c				\
	$(NULL)

gpk_application_LDADD =					\
	libgpkshared.a					\
	$(shared_LIBS)					\
	$(NULL)

gpk_prefs_SOURCES =					\
	gpk-debug.h					\
	gpk-debug.c					\
	gpk-animated-icon.c				\
	gpk-animated-icon.h				\
	gpk-enum.c					\
	gpk-enum.h					\
	gpk-common.c					\
	gpk-common.h					\
	gpk-error.c					\
	gpk-error.h					\
	gpk-prefs.c					\
	$(NULL)

gpk_prefs_LDADD =					\
	libgpkshared.a					\
	$(shared_LIBS)					\
	-lm

gpk_update_viewer_SOURCES =				\
	gpk-update-viewer.c				\
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

gpk_log_SOURCES =					\
	gpk-log.c					\
	$(NULL)

gpk_log_LDADD =						\
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
	egg-string.c					\
	egg-string.h					\
	egg-markdown.c					\
	egg-markdown.h					\
	gpk-debug.c					\
	gpk-debug.h					\
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

gpk_self_test_CFLAGS = $(AM_CFLAGS)

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

