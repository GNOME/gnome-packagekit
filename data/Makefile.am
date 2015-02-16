NULL =

SUBDIRS = appdata icons

migrationdir = $(datadir)/GConf/gsettings
dist_migration_DATA = org.gnome.packagekit.gschema.migrate

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	gpk-install-local-file.desktop.in		\
	gpk-prefs.desktop.in				\
	gpk-application.desktop.in			\
	gpk-update-viewer.desktop.in			\
	gpk-log.desktop.in				\
	$(NULL)

desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

pkgdata_DATA =						\
	gpk-eula.ui					\
	gpk-application.ui				\
	gpk-update-viewer.ui				\
	gpk-prefs.ui					\
	gpk-signature.ui				\
	gpk-error.ui					\
	gpk-log.ui					\
	gpk-client.ui					\
	$(NULL)

@GSETTINGS_RULES@
gsettings_SCHEMAS = org.gnome.packagekit.gschema.xml

EXTRA_DIST =						\
	$(gsettings_SCHEMAS)				\
	$(autostart_in_files)				\
	$(desktop_in_files)				\
	$(pkgdata_DATA)					\
	$(service_in_files)				\
	$(NULL)

clean-local :
	rm -f *~

DISTCLEANFILES =					\
	gpk-prefs.desktop				\
	gpk-update-viewer.desktop			\
	gpk-install-local-file.desktop			\
	gpk-log.desktop					\
	gpk-application.desktop				\
	org.freedesktop.PackageKit.service		\
	$(NULL)

