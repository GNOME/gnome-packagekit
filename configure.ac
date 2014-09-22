AC_PREREQ(2.65)

AC_INIT([gnome-packagekit],[3.14.0])
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-xz tar-ustar])
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

# set up gtk-doc
GTK_DOC_CHECK(1.9)

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

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------
if test "$GCC" = "yes"; then
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
#CPPFLAGS="$CPPFLAGS -g"
#CPPFLAGS="$CPPFLAGS -fexceptions"

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
GLIB_GSETTINGS

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(PACKAGEKIT, packagekit-glib2 >= 0.7.0)
PKG_CHECK_MODULES(GLIB, \
 glib-2.0 >= 2.32.0
 gobject-2.0
 gio-2.0 >= 2.25.9
 gio-unix-2.0)
PKG_CHECK_MODULES(DBUS, \
 dbus-glib-1 >= 0.73 \
 dbus-1 >= 1.1.2 \
 gthread-2.0)
PKG_CHECK_MODULES(GTK, \
 gtk+-3.0 >= 2.91.0 gdk-3.0 fontconfig)
PKG_CHECK_MODULES(CANBERRA, libcanberra-gtk3 >= 0.10)
PKG_CHECK_MODULES(X11, x11)

dnl **** Check for libnotify ****
PKG_CHECK_MODULES(NOTIFY, libnotify >= 0.7.0)

dnl ---------------------------------------------------------------------------
dnl - GUdev integration (default enabled)
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gudev, AS_HELP_STRING([--disable-gudev],[Disable GUdev support]), enable_gudev=$enableval)
if test x$enable_gudev != xno; then
	PKG_CHECK_MODULES(GUDEV, gudev-1.0, HAVE_GUDEV="yes", HAVE_GUDEV="no")
	if test "x$HAVE_GUDEV" = "xyes"; then
		AC_DEFINE(HAVE_GUDEV, 1, [define if GUdev is available])
	else
		if test x$enable_gudev = xyes; then
			AC_MSG_ERROR([GUdev enabled but not found])
		fi
	fi
else
	HAVE_GUDEV=no
fi
AM_CONDITIONAL(HAVE_GUDEV, test x$HAVE_GUDEV = xyes)

dnl ---------------------------------------------------------------------------
dnl systemd integration
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE([systemd],
              AS_HELP_STRING([--enable-systemd], [Use systemd]),
              [with_systemd=$enableval],
              [with_systemd=auto])

PKG_CHECK_MODULES(SYSTEMD,
                  [libsystemd-login polkit-gobject-1],
                  [have_systemd=yes], [have_systemd=no])

AC_MSG_CHECKING([whether to use systemd])

if test x$with_systemd = xauto ; then
        if test x$have_systemd = xno ; then
                with_systemd=no
        else
                with_systemd=yes
        fi
fi

AC_MSG_RESULT($with_systemd)

if test x$with_systemd = xyes; then
        if test x$have_systemd = xno; then
                AC_MSG_ERROR([Systemd support explicitly required, but systemd not found])
        fi
        AC_DEFINE(HAVE_SYSTEMD, 1, [Define if systemd is used for session tracking])
fi

AC_SUBST(SYSTEMD_CFLAGS)
AC_SUBST(SYSTEMD_LIBS)

AM_CONDITIONAL(WITH_SYSTEMD, [test "$with_systemd" = "yes"], [Using systemd])

dnl ---------------------------------------------------------------------------
dnl - Enable small form factor code
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(small_form_factor, AS_HELP_STRING([--enable-small-form-factor],[enable small form factor code]),
	      enable_small_form_factor=$enableval,enable_small_form_factor=no)
AM_CONDITIONAL(PK_BUILD_SMALL_FORM_FACTOR, test x$enable_small_form_factor = xyes)
if test x$enable_small_form_factor = xyes; then
	AC_DEFINE(PK_BUILD_SMALL_FORM_FACTOR,1,[Build small form factor code])
fi

dnl ---------------------------------------------------------------------------
dnl - Make paths available for source files
dnl ---------------------------------------------------------------------------
AC_SUBST(SYSCONFDIR, $sysconfdir)
AC_SUBST(DATADIR, $datadir)
AC_SUBST(BINDIR, $bindir)
AC_SUBST(SBINDIR, $sbindir)

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
man/Makefile
po/Makefile.in
data/Makefile
data/appdata/Makefile
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
"

