themedir = $(pkgdatadir)/icons/hicolor
size = 16x16
context = status

iconsdir = $(themedir)/$(size)/$(context)

icons_DATA = 				\
	pk-collection-installed.png	\
	pk-collection-available.png	\
	pk-package-installed.png	\
	pk-package-available.png	\
	pk-package-add.png		\
	pk-package-cleanup.png		\
	pk-package-blocked.png		\
	pk-package-delete.png		\
	pk-package-download.png		\
	pk-package-search.png		\
	pk-package-update.png		\
	pk-package-info.png		\
	pk-package-sources.png		\
	pk-update-bugfix.png		\
	pk-update-enhancement.png	\
	pk-update-high.png		\
	pk-update-low.png		\
	pk-update-normal.png		\
	pk-update-security.png		\
	pk-refresh-cache.png		\
	pk-rollback.png			\
	pk-service-pack.png		\
	pk-setup.png			\
	pk-wait.png

install-data-hook:
	mkdir -p $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps;
	mkdir -p $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/mimetypes;
	ln -fs $(iconsdir)/pk-package-sources.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/x-system-software-sources.png;
	ln -fs $(iconsdir)/pk-package-info.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/x-system-software-log.png;
	ln -fs $(iconsdir)/pk-update-high.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/x-system-software-update-preferences.png;
	ln -fs $(iconsdir)/pk-service-pack.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/x-system-software-servicepack.png;
	ln -fs $(iconsdir)/pk-service-pack.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/mimetypes/application-x-servicepack.png;

EXTRA_DIST =				\
	$(icons_DATA)


