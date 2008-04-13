EXTRA_DIST =				\
	pkcon.sgml

if HAVE_DOCBOOK2MAN
man_MANS =				\
	pkcon.1
endif

if HAVE_DOCBOOK2MAN
pkcon.1: pkcon.sgml
	docbook2man $? > /dev/null
endif

clean-local :
	rm -f *~
	rm -f *.1
	rm -f manpage.*

