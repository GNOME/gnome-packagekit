NULL =

SUBDIRS = icons

migrationdir = $(datadir)/GConf/gsettings
dist_migration_DATA = org.gnome.packagekit.gschema.migrate

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	gpk-install-file.desktop.in			\
	gpk-install-catalog.desktop.in			\
	gpk-service-pack.desktop.in			\
	gpk-prefs.desktop.in				\
	gpk-application.desktop.in			\
	gpk-update-viewer.desktop.in			\
	gpk-log.desktop.in				\
	$(NULL)

desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@
autostartdir = $(datadir)/gnome/autostart
autostart_in_files = gpk-update-icon.desktop.in
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

pkgdata_DATA =						\
	gpk-eula.ui					\
	gpk-application.ui				\
	gpk-update-viewer.ui				\
	gpk-prefs.ui					\
	gpk-repo.ui					\
	gpk-service-pack.ui				\
	gpk-signature.ui				\
	gpk-error.ui					\
	gpk-log.ui					\
	gpk-backend-status.ui				\
	gpk-client.ui					\
	$(NULL)

servicedir = $(datadir)/dbus-1/services
service_in_files = org.freedesktop.PackageKit.service.in
service_DATA = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(bindir)|" $< > $@

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
	gpk-service-pack.desktop			\
	gpk-update-viewer.desktop			\
	gpk-install-catalog.desktop			\
	gpk-install-file.desktop			\
	gpk-log.desktop					\
	gpk-update-icon.desktop				\
	gpk-application.desktop				\
	org.freedesktop.PackageKit.service			\
	$(NULL)

