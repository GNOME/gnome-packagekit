BUILT_SOURCES = genums.py

NULL =

genums.py: ../enum-convertor.py ../../src/gpk-enum.c
	(cd ..; python enum-convertor.py > packagekit/genums.py)

packagekitpythondir = ${PYTHON_PACKAGE_DIR}
packagekitpython_PYTHON =				\
	gtkwidgets.py					\
	genums.py					\
	$(NULL)

clean-local :
	rm -f *.pyc
	rm -f *~

