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
	gpk-repo.glade					\
	gpk-eula.glade					\
	gpk-application.glade				\
	gpk-update-viewer.glade				\
	gpk-prefs.glade					\
	gpk-service-pack.glade				\
	gpk-signature.glade				\
	gpk-error.glade					\
	gpk-log.glade					\
	gpk-backend-status.glade			\
	gpk-client.glade				\
	$(NULL)

EXTRA_DIST =						\
	$(schema_in_files)				\
	$(desktop_in_files)				\
	$(autostart_in_files)				\
	$(pkgdata_DATA)					\
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
	$(NULL)

