EXTRA_DIST =						\
	gpk-application.sgml				\
	gpk-dbus-service.sgml				\
	gpk-install-local-file.sgml			\
	gpk-log.sgml					\
	gpk-prefs.sgml					\
	gpk-update-viewer.sgml

if HAVE_DOCBOOK2MAN
man_MANS =						\
	gpk-application.1				\
	gpk-dbus-service.1				\
	gpk-install-local-file.1			\
	gpk-log.1					\
	gpk-prefs.1					\
	gpk-update-viewer.1
endif

if HAVE_DOCBOOK2MAN
gpk-application.1: gpk-application.sgml
	docbook2man $? > /dev/null
gpk-install-local-file.1: gpk-install-local-file.sgml
	docbook2man $? > /dev/null
gpk-prefs.1: gpk-prefs.sgml
	docbook2man $? > /dev/null
gpk-dbus-service.1: gpk-dbus-service.sgml
	docbook2man $? > /dev/null
gpk-update-viewer.1: gpk-update-viewer.sgml
	docbook2man $? > /dev/null
gpk-log.1: gpk-log.sgml
	docbook2man $? > /dev/null
endif

clean-local :
	rm -f *~
	rm -f *.1
	rm -f manpage.*

