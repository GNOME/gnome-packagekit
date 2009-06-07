AC_PREREQ(2.52)

AC_INIT(gnome-packagekit, 2.27.3)
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)
AC_CONFIG_MACRO_DIR([m4])

# enable nice build output on automake1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_LIBTOOL
AM_PROG_CC_C_O
IT_PROG_INTLTOOL([0.35.0])

GNOME_COMPILE_WARNINGS
GNOME_DOC_INIT

# set up gtk-doc
GTK_DOC_CHECK(1.9)

AM_PATH_PYTHON
PYTHON_PACKAGE_DIR=${pythondir}/packagekit
AC_SUBST(PYTHON_PACKAGE_DIR)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------
if test "$GCC" = "yes"; then
#	CPPFLAGS="$CPPFLAGS -DG_DISABLE_SINGLE_INCLUDES -DGTK_DISABLE_SINGLE_INCLUDES"
	CPPFLAGS="$CPPFLAGS -DG_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED"
	CPPFLAGS="$CPPFLAGS -DGDK_MULTIHEAD_SAFE -DGTK_MULTIHEAD_SAFE"
	CPPFLAGS="$CPPFLAGS -DGSEAL_ENABLE"
	CPPFLAGS="$CPPFLAGS -Wall -Wreturn-type"
	CPPFLAGS="$CPPFLAGS -Werror"
#	CPPFLAGS="$CPPFLAGS -Wextra"
	CPPFLAGS="$CPPFLAGS -Wdeclaration-after-statement"
#	CPPFLAGS="$CPPFLAGS -Wmissing-declarations"
	CPPFLAGS="$CPPFLAGS -Wno-uninitialized"
	CPPFLAGS="$CPPFLAGS -Wredundant-decls"
	CPPFLAGS="$CPPFLAGS -Wshadow"
	CPPFLAGS="$CPPFLAGS -Wpointer-arith"
	CPPFLAGS="$CPPFLAGS -Wcast-align"
	CPPFLAGS="$CPPFLAGS -Wwrite-strings"
	CPPFLAGS="$CPPFLAGS -Winline"
	CPPFLAGS="$CPPFLAGS -Wformat-nonliteral"
	CPPFLAGS="$CPPFLAGS -Wformat-security"
	CPPFLAGS="$CPPFLAGS -Wswitch-default"
	CPPFLAGS="$CPPFLAGS -Winit-self"
	CPPFLAGS="$CPPFLAGS -Wmissing-include-dirs"
	CPPFLAGS="$CPPFLAGS -Wno-strict-aliasing"
	CPPFLAGS="$CPPFLAGS -Wundef"
	CPPFLAGS="$CPPFLAGS -Waggregate-return"
	CPPFLAGS="$CPPFLAGS -Wmissing-format-attribute"
fi

dnl ---------------------------------------------------------------------------
dnl - Debugging switches (uncomment this if you want to use gdb)
dnl ---------------------------------------------------------------------------
CPPFLAGS="$CPPFLAGS -g"
CPPFLAGS="$CPPFLAGS -fexceptions"

dnl ---------------------------------------------------------------------------
dnl - xsltproc
dnl ---------------------------------------------------------------------------
AC_PATH_PROG([XSLTPROC], [xsltproc])

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
PACKAGEKIT_REQUIRED=0.4.4 # when changing, nuke some compatability entries in gpk-enum.h
GLIB_REQUIRED=2.14.0
GTK_REQUIRED=2.15.0
DBUS_REQUIRED=1.1.2
GCONF_REQUIRED=0.22
DBUS_GLIB_REQUIRED=0.73
LIBNOTIFY_REQUIRED=0.4.3
POLKIT_GNOME_REQUIRED=0.8
UNIQUE_REQUIRED=1.0.0
GNOME_MENUS_REQUIRED=2.24.1
GIO_REQUIRED=2.18.0
DEVKIT_REQUIRED=001
DEVKIT_POWER_REQUIRED=007
CANBERRA_REQUIRED=0.10

dnl ---------------------------------------------------------------------------
dnl - Make above strings available for packaging files (e.g. rpm spec files)
dnl ---------------------------------------------------------------------------
AC_SUBST(PACKAGEKIT_REQUIRED)
AC_SUBST(GLIB_REQUIRED)
AC_SUBST(GTK_REQUIRED)
AC_SUBST(DBUS_REQUIRED)
AC_SUBST(DBUS_GLIB_REQUIRED)
AC_SUBST(LIBNOTIFY_REQUIRED)
AC_SUBST(POLKIT_GNOME_REQUIRED)
AC_SUBST(UNIQUE_REQUIRED)
AC_SUBST(GNOME_MENUS_REQUIRED)
AC_SUBST(GIO_REQUIRED)

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(PACKAGEKIT, packagekit-glib >= $PACKAGEKIT_REQUIRED)
AC_SUBST(PACKAGEKIT_CFLAGS)
AC_SUBST(PACKAGEKIT_LIBS)

PKG_CHECK_MODULES(POLKIT_GNOME, polkit-gnome >= $POLKIT_GNOME_REQUIRED)
AC_SUBST(POLKIT_GNOME_CFLAGS)
AC_SUBST(POLKIT_GNOME_LIBS)

PKG_CHECK_MODULES(GLIB, glib-2.0 >= $GLIB_REQUIRED gobject-2.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GIO, gio-2.0 >= $GIO_REQUIRED)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(DBUS, \
 dbus-glib-1 >= $DBUS_GLIB_REQUIRED \
 dbus-1 >= $DBUS_REQUIRED \
 gthread-2.0)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(GTK, \
 gtk+-2.0 >= $GTK_REQUIRED gdk-2.0 fontconfig)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(GCONF, \
 gconf-2.0 >= $GCONF_REQUIRED)
AC_SUBST(GCONF_CFLAGS)
AC_SUBST(GCONF_LIBS)

PKG_CHECK_MODULES(LIBNOTIFY, \
 libnotify >= $LIBNOTIFY_REQUIRED)
AC_SUBST(LIBNOTIFY_CFLAGS)
AC_SUBST(LIBNOTIFY_LIBS)

PKG_CHECK_MODULES(GNOME_MENUS, \
 libgnome-menu >= $GNOME_MENUS_REQUIRED)
AC_SUBST(GNOME_MENUS_CFLAGS)
AC_SUBST(GNOME_MENUS_LIBS)

PKG_CHECK_MODULES(UNIQUE, \
 unique-1.0 >= $UNIQUE_REQUIRED)
AC_SUBST(UNIQUE_CFLAGS)
AC_SUBST(UNIQUE_LIBS)

PKG_CHECK_MODULES(DEVKIT, [
 devkit-gobject >= $DEVKIT_REQUIRED
 devkit-power-gobject >= $DEVKIT_POWER_REQUIRED])
AC_SUBST(DEVKIT_CFLAGS)
AC_SUBST(DEVKIT_LIBS)

PKG_CHECK_MODULES(CANBERRA, libcanberra-gtk >= $CANBERRA_REQUIRED)
AC_SUBST(CANBERRA_CFLAGS)
AC_SUBST(CANBERRA_LIBS)

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
docs/dbus/Makefile
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
python/Makefile
python/packagekit/Makefile
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
        cppflags:                  ${CPPFLAGS}
        gconf-schema dir:          $GCONF_SCHEMA_FILE_DIR
"

