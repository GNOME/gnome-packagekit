themedir = $(pkgdatadir)/icons/hicolor
size = scalable
context = status

iconsdir = $(themedir)/$(size)/$(context)

icons_DATA = 				\
	pk-collection-installed.svg	\
	pk-collection-available.svg	\
	pk-package-installed.svg	\
	pk-package-available.svg	\
	pk-package-add.svg		\
	pk-package-cleanup.svg		\
	pk-package-blocked.svg		\
	pk-package-delete.svg		\
	pk-package-download.svg		\
	pk-package-downloaded.svg	\
	pk-package-search.svg		\
	pk-package-update.svg		\
	pk-package-info.svg		\
	pk-package-sources.svg		\
	pk-update-bugfix.svg		\
	pk-update-enhancement.svg	\
	pk-update-high.svg		\
	pk-update-low.svg		\
	pk-update-normal.svg		\
	pk-update-security.svg		\
	pk-refresh-cache.svg		\
	pk-setup.svg			\
	pk-wait.svg

install-data-hook:
	mkdir -p $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps;
	mkdir -p $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/mimetypes;
	ln -fs $(iconsdir)/pk-package-sources.svg $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/gpk-repo.svg;
	ln -fs $(iconsdir)/pk-package-info.svg $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/gpk-log.svg
	ln -fs $(iconsdir)/pk-update-high.svg $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/apps/gpk-prefs.svg;
	ln -fs $(iconsdir)/pk-package-info.svg $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/mimetypes/application-x-package-list.svg;
	ln -fs $(iconsdir)/pk-collection-installed.svg $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/mimetypes/application-x-catalog.svg;

EXTRA_DIST =				\
	$(icons_DATA)


