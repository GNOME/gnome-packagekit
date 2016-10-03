@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/metainfo
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files =					\
	org.gnome.Packages.appdata.xml.in		\
	org.gnome.PackageUpdater.appdata.xml.in
EXTRA_DIST = $(appdata_in_files)
CLEANFILES = $(appdata_DATA)

-include $(top_srcdir)/git.mk
