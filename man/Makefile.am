EXTRA_DIST =						\
	gpk-application.sgml				\
	gpk-backend-status.sgml				\
	gpk-install-local-file.sgml			\
	gpk-install-mime-type.sgml			\
	gpk-install-package-name.sgml			\
	gpk-install-provide-file.sgml			\
	gpk-repo.sgml					\
	gpk-update-icon.sgml				\
	gpk-update-viewer.sgml

if HAVE_DOCBOOK2MAN
man_MANS =						\
	gpk-application.1				\
	gpk-backend-status.1				\
	gpk-install-local-file.1			\
	gpk-install-mime-type.1			\
	gpk-install-package-name.1			\
	gpk-install-provide-file.1			\
	gpk-repo.1					\
	gpk-update-icon.1				\
	gpk-update-viewer.1
endif

if HAVE_DOCBOOK2MAN
gpk-application.1: gpk-application.sgml
	docbook2man $? > /dev/null
gpk-backend-status.1: gpk-backend-status.sgml
	docbook2man $? > /dev/null
gpk-install-local-file.1: gpk-install-local-file.sgml
	docbook2man $? > /dev/null
gpk-install-package-name.1: gpk-install-package-name.sgml
	docbook2man $? > /dev/null
gpk-install-provide-file.1: gpk-install-provide-file.sgml
	docbook2man $? > /dev/null
gpk-install-mime-type.1: gpk-install-mime-type.sgml
	docbook2man $? > /dev/null
gpk-repo.1: gpk-repo.sgml
	docbook2man $? > /dev/null
gpk-update-icon.1: gpk-update-icon.sgml
	docbook2man $? > /dev/null
gpk-update-viewer.1: gpk-update-viewer.sgml
	docbook2man $? > /dev/null
endif

clean-local :
	rm -f *~
	rm -f *.1
	rm -f manpage.*

