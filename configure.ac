AC_PREREQ(2.52)

AC_INIT(gnome-packagekit, 0.3.5)
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_LIBTOOL
AM_PROG_CC_C_O
IT_PROG_INTLTOOL([0.35.0])

GNOME_COMPILE_WARNINGS
GNOME_DOC_INIT

# set up gtk-doc
GTK_DOC_CHECK(1.9)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------
if test "$GCC" = "yes"; then
	CPPFLAGS="$CPPFLAGS -Werror -Wcast-align -Wno-uninitialized"
	CPPFLAGS="$CPPFLAGS -Wall -Wformat-security"
#	CPPFLAGS="$CPPFLAGS -DG_DISABLE_SINGLE_INCLUDES -DGTK_DISABLE_SINGLE_INCLUDES"
	CPPFLAGS="$CPPFLAGS -DG_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED"
	CPPFLAGS="$CPPFLAGS -DGDK_MULTIHEAD_SAFE -DGTK_MULTIHEAD_SAFE"
fi

dnl ---------------------------------------------------------------------------
dnl - Debugging switches (uncomment this if you want to use gdb)
dnl ---------------------------------------------------------------------------
CPPFLAGS="$CPPFLAGS -g"
CPPFLAGS="$CPPFLAGS -fexceptions"

dnl ---------------------------------------------------------------------------
dnl - gettext stuff
dnl ---------------------------------------------------------------------------
GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE(GETTEXT_PACKAGE, "AC_PACKAGE_NAME", [foo])

AM_GLIB_GNU_GETTEXT

dnl ---------------------------------------------------------------------------
dnl - Library dependencies
dnl ---------------------------------------------------------------------------
PACKAGEKIT_REQUIRED=0.3.3
GLIB_REQUIRED=2.14.0
GTK_REQUIRED=2.12.0
DBUS_REQUIRED=1.1.2
GCONF_REQUIRED=0.22
DBUS_GLIB_REQUIRED=0.73
LIBGLADE_REQUIRED=2.5.0
LIBNOTIFY_REQUIRED=0.4.3
LIBSEXY_REQUIRED=0.1.10
POLKIT_GNOME_REQUIRED=0.8
UNIQUE_REQUIRED=0.9.4

dnl ---------------------------------------------------------------------------
dnl - Make above strings available for packaging files (e.g. rpm spec files)
dnl ---------------------------------------------------------------------------
AC_SUBST(PACKAGEKIT_REQUIRED)
AC_SUBST(GLIB_REQUIRED)
AC_SUBST(GTK_REQUIRED)
AC_SUBST(DBUS_REQUIRED)
AC_SUBST(DBUS_GLIB_REQUIRED)
AC_SUBST(LIBGLADE_REQUIRED)
AC_SUBST(LIBNOTIFY_REQUIRED)
AC_SUBST(LIBSEXY_REQUIRED)
AC_SUBST(POLKIT_GNOME_REQUIRED)
AC_SUBST(UNIQUE_REQUIRED)

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(PACKAGEKIT, packagekit >= $PACKAGEKIT_REQUIRED)
AC_SUBST(PACKAGEKIT_CFLAGS)
AC_SUBST(PACKAGEKIT_LIBS)

PKG_CHECK_MODULES(POLKIT_GNOME, polkit-gnome >= $POLKIT_GNOME_REQUIRED)
AC_SUBST(POLKIT_GNOME_CFLAGS)
AC_SUBST(POLKIT_GNOME_LIBS)

PKG_CHECK_MODULES(GLIB, glib-2.0 >= $GLIB_REQUIRED gobject-2.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(DBUS, \
 dbus-glib-1 >= $DBUS_GLIB_REQUIRED \
 dbus-1 >= $DBUS_REQUIRED \
 gthread-2.0)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(GTK, \
 gtk+-2.0 >= $GTK_REQUIRED, gdk-2.0)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(LIBSEXY, \
 libsexy >= $LIBSEXY_REQUIRED)
AC_SUBST(LIBSEXY_CFLAGS)
AC_SUBST(LIBSEXY_LIBS)

PKG_CHECK_MODULES(LIBGLADE, \
 libglade-2.0 >= $LIBGLADE_REQUIRED)
AC_SUBST(LIBGLADE_CFLAGS)
AC_SUBST(LIBGLADE_LIBS)

PKG_CHECK_MODULES(GCONF, \
 gconf-2.0 >= $GCONF_REQUIRED)
AC_SUBST(GCONF_CFLAGS)
AC_SUBST(GCONF_LIBS)

PKG_CHECK_MODULES(LIBNOTIFY, \
 libnotify >= $LIBNOTIFY_REQUIRED)
AC_SUBST(LIBNOTIFY_CFLAGS)
AC_SUBST(LIBNOTIFY_LIBS)

AC_PATH_PROG(GCONFTOOL, gconftool-2)
AM_GCONF_SOURCE_2

AC_PATH_XTRA

ALL_X_LIBS="$X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"

dnl ---------------------------------------------------------------------------
dnl - Make paths available for source files
dnl ---------------------------------------------------------------------------
AC_SUBST(SYSCONFDIR, $sysconfdir)
AC_SUBST(DATADIR, $datadir)
AC_SUBST(BINDIR, $bindir)
AC_SUBST(SBINDIR, $sbindir)

dnl ---------------------------------------------------------------------------
dnl - Where should we put documentation ?
dnl ---------------------------------------------------------------------------
AC_ARG_WITH(doc-dir,
              [AC_HELP_STRING([--with-doc-dir=<dir>],
              [directory to install documentation])])
if ! test -z "$with_doc_dir"; then
   DOCDIR="$with_doc_dir/gnome-packagekit-$VERSION"
else
   DOCDIR="$DATADIR/doc/gnome-packagekit-$VERSION"
fi
AC_SUBST(DOCDIR)

dnl ---------------------------------------------------------------------------
dnl - Is docbook2man available?
dnl ---------------------------------------------------------------------------
AC_PATH_PROG(DOCBOOK2MAN, docbook2man, no)
if test "$DOCBOOK2MAN" = "no" ; then
	AC_MSG_WARN([docbook2man not found, will not be able to build man documentation])
	fi
AM_CONDITIONAL(HAVE_DOCBOOK2MAN, [test "$DOCBOOK2MAN" != "no"])

dnl ---------------------------------------------------------------------------
dnl - Is unique available?
dnl ---------------------------------------------------------------------------
if $PKG_CONFIG --atleast-version $UNIQUE_REQUIRED unique-1.0; then
   have_unique=yes
   PKG_CHECK_MODULES(UNIQUE, unique-1.0 >= $UNIQUE_REQUIRED)
   AC_SUBST(UNIQUE_CFLAGS)
   AC_SUBST(UNIQUE_LIBS)
   AC_DEFINE(HAVE_UNIQUE, 1, [Building with unique support])
else
   have_unique=no
   AC_DEFINE(HAVE_UNIQUE, 0, [Not building with unique support])
fi
AM_CONDITIONAL([HAVE_UNIQUE], [test $have_unique = yes])

dnl ---------------------------------------------------------------------------
dnl - Build self tests
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(tests, AS_HELP_STRING([--enable-tests],[enable unit test code]),
	      enable_tests=$enableval,enable_tests=no)
AM_CONDITIONAL(EGG_BUILD_TESTS, test x$enable_tests = xyes)
if test x$enable_tests = xyes; then
	AC_DEFINE(EGG_BUILD_TESTS,1,[Build test code])
fi

dnl ---------------------------------------------------------------------------
dnl - Other tests
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gcov, AS_HELP_STRING([--enable-gcov],[compile with coverage profiling instrumentation (gcc only)]),
	      enable_gcov=$enableval,enable_gcov=no)
AC_ARG_ENABLE(gprof, AS_HELP_STRING([--enable-gprof],[compile with gprof support (gcc only)]),
	      enable_gprof=$enableval,enable_gprof=no)

if test x$enable_gcov = xyes; then
	## so that config.h changes when you toggle gcov support
	AC_DEFINE_UNQUOTED(PK_BUILD_GCOV, 1, [Defined if gcov is enabled to force a rebuild due to config.h changing])

	AC_MSG_CHECKING([for gcc 3.3 version of gcov file format])
	have_gcc33_gcov=no
	AC_RUN_IFELSE( [AC_LANG_PROGRAM( , [[ if (__GNUC__ >=3 && __GNUC_MINOR__ >= 3) exit (0); else exit (1); ]])],
			have_gcc33_gcov=yes)
	if test x$have_gcc33_gcov = xyes ; then
		AC_DEFINE_UNQUOTED(PK_HAVE_GCC33_GCOV, 1, [Defined if we have gcc 3.3 and thus the new gcov format])
	fi
	CFLAGS="$CFLAGS -fprofile-arcs -ftest-coverage"
	AC_MSG_RESULT($have_gcc33_gcov)
fi
AM_CONDITIONAL(PK_BUILD_GCOV, test x$enable_gcov = xyes)

if test x$enable_gprof = xyes; then
	## so that config.h changes when you toggle gprof support
	AC_DEFINE_UNQUOTED(PK_BUILD_GPROF, 1, [Defined if gprof is enabled to force a rebuild due to config.h changing])
	CPPFLAGS="$CPPFLAGS -pg"
	LDFLAGS="$LDFLAGS -pg"
	CFLAGS="$CFLAGS -fprofile-arcs -ftest-coverage"
	AC_MSG_RESULT($enable_gprof)
fi
AM_CONDITIONAL(PK_BUILD_GPROF, test x$enable_gprof = xyes)

dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_OUTPUT([
Makefile
src/Makefile
help/Makefile
man/Makefile
po/Makefile.in
data/Makefile
docs/Makefile
data/icons/Makefile
data/icons/16x16/Makefile
data/icons/22x22/Makefile
data/icons/24x24/Makefile
data/icons/48x48/Makefile
data/icons/scalable/Makefile
data/icons/16x16/animations/Makefile
data/icons/16x16/categories/Makefile
data/icons/16x16/status/Makefile
data/icons/22x22/animations/Makefile
data/icons/22x22/categories/Makefile
data/icons/22x22/status/Makefile
data/icons/24x24/animations/Makefile
data/icons/24x24/categories/Makefile
data/icons/24x24/status/Makefile
data/icons/48x48/animations/Makefile
data/icons/48x48/categories/Makefile
data/icons/48x48/status/Makefile
data/icons/scalable/categories/Makefile
data/icons/scalable/status/Makefile
])

dnl ==========================================================================
echo "
                    gnome-packagekit $VERSION
                  =============================

        GCC coverage profiling:    ${enable_gcov}
        GCC time profiling:        ${enable_gprof}
        unique support:            ${have_unique}
        prefix:                    ${prefix}
        datadir:                   ${datadir}
        compiler:                  ${CC}
        cflags:                    ${CFLAGS}
	gconf-schema dir:          $GCONF_SCHEMA_FILE_DIR
"

