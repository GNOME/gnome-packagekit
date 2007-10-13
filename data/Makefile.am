NULL =

@INTLTOOL_SCHEMAS_RULE@
schemadir = $(sysconfdir)/gconf/schemas
schema_in_files = gnome-packagekit.schemas.in
schema_DATA = $(schema_in_files:.schemas.in=.schemas)

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	pk-install-file.desktop.in			\
	pk-prefs.desktop.in				\
	pk-application.desktop.in			\
	pk-update-viewer.desktop.in			\
	pk-transaction-viewer.desktop.in		\
	$(NULL)

desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@
autostartdir = $(datadir)/gnome/autostart
autostart_in_files = pk-update-icon.desktop.in
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

pkgdata_DATA =						\
	pk-repo.glade					\
	pk-application.glade				\
	pk-update-viewer.glade				\
	pk-progress.glade				\
	pk-prefs.glade					\
	pk-signature.glade				\
	pk-transactions.glade				\
	pk-backend-status.glade				\
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
	pk-prefs.desktop				\
	pk-update-viewer.desktop			\
	pk-install-file.desktop				\
	pk-transaction-viewer.desktop			\
	pk-update-icon.desktop				\
	pk-application.desktop				\
	gnome-packagekit.schemas			\
	$(NULL)

