AC_PREREQ(2.65)

AC_INIT([gnome-packagekit],[2.31.1])
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# enable nice build output on automake1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_INSTALL
LT_INIT
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
dnl - Use strict options (default enabled for devs, disabled in releases)
dnl ---------------------------------------------------------------------------

dnl if .git directory is present, considering we are working in the repository
if test -d ".git"; then
	default_strict=yes
else
	default_strict=no
fi

AC_ARG_ENABLE(strict, AS_HELP_STRING([--enable-strict],[Enable strict compilation options]), enable_strict=$enableval,
					enable_strict=$default_strict)
if test x$enable_strict != xno; then
	if test "$GCC" = "yes"; then
		CPPFLAGS="$CPPFLAGS -Werror"
	fi
fi

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------
if test "$GCC" = "yes"; then
#	CPPFLAGS="$CPPFLAGS -DG_DISABLE_SINGLE_INCLUDES -DGTK_DISABLE_SINGLE_INCLUDES"
	CPPFLAGS="$CPPFLAGS -DG_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED"
#	CPPFLAGS="$CPPFLAGS -DGDK_MULTIHEAD_SAFE -DGTK_MULTIHEAD_SAFE"
	CPPFLAGS="$CPPFLAGS -DGSEAL_ENABLE"
	CPPFLAGS="$CPPFLAGS -Wall -Wreturn-type"
#	CPPFLAGS="$CPPFLAGS -Wextra"
	CPPFLAGS="$CPPFLAGS -Wdeclaration-after-statement"
#	CPPFLAGS="$CPPFLAGS -Wmissing-declarations"
	CPPFLAGS="$CPPFLAGS -Wno-uninitialized"
#	CPPFLAGS="$CPPFLAGS -Wredundant-decls"
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
AC_PATH_PROG(XSLTPROC, xsltproc, no)
if test "$XSLTPROC" = "no" ; then
	AC_MSG_ERROR([xsltproc has not been found])
fi

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
PACKAGEKIT_REQUIRED=0.5.5 # when changing, nuke some compatability entries in gpk-enum.h
GLIB_REQUIRED=2.14.0
GTK_REQUIRED=2.19.3
DBUS_REQUIRED=1.1.2
GCONF_REQUIRED=0.22
DBUS_GLIB_REQUIRED=0.73
LIBNOTIFY_REQUIRED=0.4.3
UNIQUE_REQUIRED=1.0.0
GNOME_MENUS_REQUIRED=2.24.1
GIO_REQUIRED=2.18.0
DEVKIT_POWER_REQUIRED=007
CANBERRA_REQUIRED=0.10

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(PACKAGEKIT, packagekit-glib2 >= $PACKAGEKIT_REQUIRED)
PKG_CHECK_MODULES(GLIB, \
 glib-2.0 >= $GLIB_REQUIRED
 gobject-2.0
 gio-2.0 >= $GIO_REQUIRED
 gio-unix-2.0 >= $GIO_REQUIRED)
PKG_CHECK_MODULES(DBUS, \
 dbus-glib-1 >= $DBUS_GLIB_REQUIRED \
 dbus-1 >= $DBUS_REQUIRED \
 gthread-2.0)
PKG_CHECK_MODULES(GTK, \
 gtk+-2.0 >= $GTK_REQUIRED gdk-2.0 fontconfig)
PKG_CHECK_MODULES(GCONF, \
 gconf-2.0 >= $GCONF_REQUIRED)
PKG_CHECK_MODULES(LIBNOTIFY, \
 libnotify >= $LIBNOTIFY_REQUIRED)
PKG_CHECK_MODULES(GNOME_MENUS, \
 libgnome-menu >= $GNOME_MENUS_REQUIRED)
PKG_CHECK_MODULES(UNIQUE, \
 unique-1.0 >= $UNIQUE_REQUIRED)
PKG_CHECK_MODULES(DEVKIT, [
 devkit-power-gobject >= $DEVKIT_POWER_REQUIRED])
PKG_CHECK_MODULES(CANBERRA, libcanberra-gtk >= $CANBERRA_REQUIRED)
PKG_CHECK_MODULES(X11, x11)

dnl ---------------------------------------------------------------------------
dnl - GUdev integration (default enabled)
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gudev, AS_HELP_STRING([--disable-gudev],[Disable GUdev support]), enable_gudev=$enableval)
if test x$enable_gudev != xno; then
	PKG_CHECK_MODULES(GUDEV, gudev-1.0, GPK_BUILD_GUDEV="yes", GPK_BUILD_GUDEV="no")
	if test "x$GPK_BUILD_GUDEV" = "xyes"; then
		AC_DEFINE(GPK_BUILD_GUDEV, 1, [define if GUdev is available])
	else
		if test x$enable_gudev = xyes; then
			AC_MSG_ERROR([GUdev enabled but not found])
		fi
	fi
else
	GPK_BUILD_GUDEV=no
fi
AM_CONDITIONAL(GPK_BUILD_GUDEV, test x$GPK_BUILD_GUDEV = xyes)

dnl ---------------------------------------------------------------------------
dnl - Enable small form factor code
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(small_form_factor, AS_HELP_STRING([--enable-small-form-factor],[enable small form factor code]),
	      enable_small_form_factor=$enableval,enable_small_form_factor=no)
AM_CONDITIONAL(PK_BUILD_SMALL_FORM_FACTOR, test x$enable_small_form_factor = xyes)
if test x$enable_small_form_factor = xyes; then
	AC_DEFINE(PK_BUILD_SMALL_FORM_FACTOR,1,[Build small form factor code])
fi

AC_PATH_PROG(GCONFTOOL, gconftool-2)
AM_GCONF_SOURCE_2

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
              [AS_HELP_STRING([--with-doc-dir=<dir>],[directory to install documentation])])
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
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_CONFIG_FILES([
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
AC_OUTPUT

dnl ==========================================================================
echo "
                    gnome-packagekit $VERSION
                  =============================

        prefix:                    ${prefix}
        compiler:                  ${CC}
        cflags:                    ${CFLAGS}
        cppflags:                  ${CPPFLAGS}
        gconf-schema dir:          $GCONF_SCHEMA_FILE_DIR
"

