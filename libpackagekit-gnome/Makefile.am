
NULL =

INCLUDES = \
	$(GTK_CFLAGS)						\
	$(GLIB_CFLAGS)						\
	$(POLKIT_GNOME_CFLAGS)					\
	$(DBUS_CFLAGS)						\
	$(GCONF_CFLAGS)						\
	$(LIBGLADE_CFLAGS)					\
	$(PACKAGEKIT_CFLAGS)					\
	$(GLIB_CFLAGS)						\
	-DDATADIR=\"$(datadir)\"				\
	-DPK_DATA=\"$(pkgdatadir)\"				\
	-I$(top_srcdir)/libselftest				\
	$(NULL)

SELFTEST_LIBS =							\
	$(top_builddir)/libselftest/libselftest.la		\
	$(NULL)

lib_LTLIBRARIES =						\
	libpackagekit-gnome.la					\
	$(NULL)

libpackagekit_gnome_includedir = $(includedir)/packagekit-gnome

libpackagekit_gnome_include_HEADERS =				\
	gpk-client.h						\
	gpk-common.h						\
	gpk-error.h						\
	$(NULL)

libpackagekit_gnome_la_SOURCES =				\
	gpk-common.c						\
	gpk-common.h						\
	gpk-client.c						\
	gpk-client.h						\
	gpk-gnome.c						\
	gpk-gnome.h						\
	gpk-error.c						\
	gpk-error.h						\
	$(NULL)

libpackagekit_gnome_la_LIBADD =					\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(GTK_LIBS)						\
	$(PACKAGEKIT_LIBS)					\
	$(POLKIT_GNOME_LIBS)					\
	$(LIBGLADE_LIBS)					\
	$(GCONF_LIBS)						\
	$(NULL)

libpackagekit_gnome_la_LDFLAGS =				\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	$(NULL)

check_PROGRAMS =						\
	gpk-self-test

noinst_PROGRAMS =						\
	gpk-self-test

gpk_self_test_SOURCES =						\
	gpk-self-test.c						\
	gpk-common.c						\
	gpk-common.h						\
	$(NULL)

gpk_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(GTK_LIBS)						\
	$(DBUS_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(PK_LIBS)						\
	$(PACKAGEKIT_LIBS)					\
	$(POLKIT_GNOME_LIBS)					\
	$(NULL)

TESTS = gpk-self-test

if PK_BUILD_GPROF
clean-gprof:
	rm -f *.out
gprof: clean-gprof all check
	gprof ./gpk-self-test > gprof.txt
endif

gpk_self_test_CPPFLAGS=	\
	-DPK_BUILD_TESTS

clean-local:
	rm -f *~
	rm -f *.out
	rm -f *.gcda
	rm -f *.gcno
	rm -f gcov.txt
	rm -f gprof.txt

