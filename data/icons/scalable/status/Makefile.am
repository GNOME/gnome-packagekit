themedir = $(pkgdatadir)/icons/hicolor
size = scalable
context = status

iconsdir = $(themedir)/$(size)/$(context)

icons_DATA = 				\
	pk-package-installed.svg	\
	pk-package-available.svg	\
	pk-package-add.svg		\
	pk-package-cleanup.svg		\
	pk-package-delete.svg		\
	pk-package-download.svg		\
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
	pk-rollback.svg			\
	pk-setup.svg			\
	pk-wait.svg

install-data-hook:
	mkdir -p $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/status;
	ln -fs $(iconsdir)/pk-package-sources.svg $(DESTDIR)$(DATADIR)/icons/hicolor/$(size)/status/;

EXTRA_DIST =				\
	$(icons_DATA)


