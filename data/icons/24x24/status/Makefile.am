themedir = $(pkgdatadir)/icons/hicolor
size = 24x24
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
	pk-package-downloaded.png	\
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
	pk-setup.png			\
	pk-wait.png

install-data-hook:
	mkdir -p $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps;
	mkdir -p $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/mimetypes;
	ln -fs $(iconsdir)/pk-package-sources.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/gpk-repo.png;
	ln -fs $(iconsdir)/pk-package-info.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/gpk-log.png;
	ln -fs $(iconsdir)/pk-update-high.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/gpk-prefs.png;
	ln -fs $(iconsdir)/pk-package-info.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/mimetypes/application-x-package-list.png;
	ln -fs $(iconsdir)/pk-collection-installed.png $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/mimetypes/application-x-catalog.png;

EXTRA_DIST =				\
	$(icons_DATA)


