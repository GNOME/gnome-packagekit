NULL =

SUBDIRS = icons

@INTLTOOL_SCHEMAS_RULE@
schemadir = $(sysconfdir)/gconf/schemas
schema_in_files = gnome-packagekit.schemas.in
schema_DATA = $(schema_in_files:.schemas.in=.schemas)

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	gpk-install-file.desktop.in			\
	gpk-install-catalog.desktop.in			\
	gpk-service-pack.desktop.in			\
	gpk-prefs.desktop.in				\
	gpk-application.desktop.in			\
	gpk-update-viewer.desktop.in			\
	gpk-repo.desktop.in				\
	gpk-log.desktop.in				\
	$(NULL)

desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@
autostartdir = $(datadir)/gnome/autostart
autostart_in_files = gpk-update-icon.desktop.in
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

pkgdata_DATA =						\
	gpk-repo.ui					\
	gpk-eula.ui					\
	gpk-application.ui				\
	gpk-update-viewer.ui				\
	gpk-prefs.ui					\
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

EXTRA_DIST =						\
	$(schema_in_files)				\
	$(desktop_in_files)				\
	$(autostart_in_files)				\
	$(pkgdata_DATA)					\
	$(service_in_files)				\
	$(NULL)

if GCONF_SCHEMAS_INSTALL
install-data-local:
	-GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) \
	$(GCONFTOOL) --makefile-install-rule $(schema_DATA)
endif

if GCONF_SCHEMAS_INSTALL
uninstall-local:
	-GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) \
	$(GCONFTOOL) --makefile-uninstall-rule $(schema_DATA)
endif

clean-local :
	rm -f *~

DISTCLEANFILES =					\
	gpk-repo.desktop				\
	gpk-prefs.desktop				\
	gpk-service-pack.desktop			\
	gpk-update-viewer.desktop			\
	gpk-install-catalog.desktop			\
	gpk-install-file.desktop			\
	gpk-log.desktop					\
	gpk-update-icon.desktop				\
	gpk-application.desktop				\
	gnome-packagekit.schemas			\
	org.freedesktop.PackageKit.service			\
	$(NULL)

