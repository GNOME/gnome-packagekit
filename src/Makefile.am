## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(GTK_CFLAGS)					\
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
	-DG_LOG_DOMAIN=\"GnomePackageKit\"

bin_PROGRAMS =						\
	gpk-application					\
	gpk-prefs					\
	gpk-update-viewer				\
	gpk-log

noinst_LIBRARIES = libgpkshared.a
libgpkshared_a_SOURCES =				\
	gpk-debug.c					\
	gpk-debug.h					\
	gpk-enum.c					\
	gpk-enum.h					\
	gpk-dialog.c					\
	gpk-dialog.h					\
	gpk-common.c					\
	gpk-common.h					\
	gpk-task.c					\
	gpk-task.h					\
	gpk-error.c					\
	gpk-error.h

if WITH_SYSTEMD
libgpkshared_a_SOURCES +=				\
	systemd-proxy.c					\
	systemd-proxy.h
endif

shared_LIBS =						\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(GTK_LIBS)					\
	$(GUDEV_LIBS)					\
	$(NOTIFY_LIBS)					\
	$(SYSTEMD_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(GNOME_MENUS_LIBS)				\
	$(CANBERRA_LIBS)

gpk_application_SOURCES =				\
	gpk-application.c				\
	gpk-application-resources.c			\
	gpk-application-resources.h

gpk_application_LDADD =					\
	libgpkshared.a					\
	$(shared_LIBS)

gpk_application_CFLAGS =				\
	$(WARN_CFLAGS)

gpk_prefs_SOURCES =					\
	gpk-debug.h					\
	gpk-debug.c					\
	gpk-enum.c					\
	gpk-enum.h					\
	gpk-common.c					\
	gpk-common.h					\
	gpk-error.c					\
	gpk-error.h					\
	gpk-prefs.c					\
	gpk-prefs-resources.c				\
	gpk-prefs-resources.h

gpk_prefs_LDADD =					\
	libgpkshared.a					\
	$(shared_LIBS)					\
	-lm

gpk_prefs_CFLAGS =					\
	$(WARN_CFLAGS)

gpk_update_viewer_SOURCES =				\
	gpk-update-viewer.c				\
	gpk-update-viewer-resources.c			\
	gpk-update-viewer-resources.h			\
	gpk-cell-renderer-size.c			\
	gpk-cell-renderer-size.h			\
	gpk-cell-renderer-info.c			\
	gpk-cell-renderer-info.h			\
	gpk-cell-renderer-restart.c			\
	gpk-cell-renderer-restart.h

gpk_update_viewer_LDADD =				\
	libgpkshared.a					\
	$(shared_LIBS)

gpk_update_viewer_CFLAGS =				\
	$(WARN_CFLAGS)

gpk_log_SOURCES =					\
	gpk-log.c					\
	gpk-log-resources.c				\
	gpk-log-resources.h

gpk_log_LDADD =						\
	libgpkshared.a					\
	$(shared_LIBS)

gpk_log_CFLAGS =					\
	$(WARN_CFLAGS)

if EGG_BUILD_TESTS

check_PROGRAMS =					\
	gpk-self-test

noinst_PROGRAMS =					\
	gpk-self-test

gpk_self_test_SOURCES =					\
	gpk-self-test.c					\
	gpk-debug.c					\
	gpk-debug.h					\
	gpk-enum.c					\
	gpk-enum.h					\
	gpk-common.c					\
	gpk-common.h					\
	gpk-error.c					\
	gpk-error.h					\
	gpk-task.c					\
	gpk-task.h					\
	gpk-dialog.c					\
	gpk-dialog.h

gpk_self_test_LDADD =					\
	$(shared_LIBS)					\
	$(SELFTEST_LIBS)

gpk_self_test_CFLAGS =					\
	$(WARN_CFLAGS)

TESTS = gpk-self-test
endif

gpk-application-resources.c: gpk-application.gresource.xml gpk-application.ui
	$(AM_V_GEN) glib-compile-resources --target=$@ --generate-source --c-name gpk --sourcedir=$(srcdir) $(srcdir)/gpk-application.gresource.xml
gpk-application-resources.h: gpk-application.gresource.xml
	$(AM_V_GEN) glib-compile-resources --target=$@ --generate-header --c-name gpk --sourcedir=$(srcdir) $(srcdir)/gpk-application.gresource.xml

gpk-log-resources.c: gpk-log.gresource.xml gpk-log.ui
	$(AM_V_GEN) glib-compile-resources --target=$@ --generate-source --c-name gpk --sourcedir=$(srcdir) $(srcdir)/gpk-log.gresource.xml
gpk-log-resources.h: gpk-log.gresource.xml
	$(AM_V_GEN) glib-compile-resources --target=$@ --generate-header --c-name gpk --sourcedir=$(srcdir) $(srcdir)/gpk-log.gresource.xml

gpk-prefs-resources.c: gpk-prefs.gresource.xml gpk-prefs.ui
	$(AM_V_GEN) glib-compile-resources --target=$@ --generate-source --c-name gpk --sourcedir=$(srcdir) $(srcdir)/gpk-prefs.gresource.xml
gpk-prefs-resources.h: gpk-prefs.gresource.xml
	$(AM_V_GEN) glib-compile-resources --target=$@ --generate-header --c-name gpk --sourcedir=$(srcdir) $(srcdir)/gpk-prefs.gresource.xml

gpk-update-viewer-resources.c: gpk-update-viewer.gresource.xml gpk-update-viewer.ui
	$(AM_V_GEN) glib-compile-resources --target=$@ --generate-source --c-name gpk --sourcedir=$(srcdir) $(srcdir)/gpk-update-viewer.gresource.xml
gpk-update-viewer-resources.h: gpk-update-viewer.gresource.xml
	$(AM_V_GEN) glib-compile-resources --target=$@ --generate-header --c-name gpk --sourcedir=$(srcdir) $(srcdir)/gpk-update-viewer.gresource.xml

BUILT_SOURCES =						\
	gpk-application-resources.c			\
	gpk-application-resources.h			\
	gpk-log-resources.c				\
	gpk-log-resources.h				\
	gpk-prefs-resources.c				\
	gpk-prefs-resources.h				\
	gpk-update-viewer-resources.c			\
	gpk-update-viewer-resources.h

EXTRA_DIST =						\
	gpk-application.gresource.xml			\
	gpk-application.ui				\
	gpk-client.ui					\
	gpk-error.ui					\
	gpk-eula.ui					\
	gpk-log.gresource.xml				\
	gpk-log.ui					\
	gpk-prefs.gresource.xml				\
	gpk-prefs.ui					\
	gpk-signature.ui				\
	gpk-update-viewer.gresource.xml			\
	gpk-update-viewer.ui

clean-local:
	rm -f *~
	rm -f *.out
	rm -f *.gcda
	rm -f *.gcno
	rm -f gcov.txt
	rm -f gprof.txt

CLEANFILES = *~ $(BUILT_SOURCES) $(noinst_LIBRARIES)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in

-include $(top_srcdir)/git.mk
