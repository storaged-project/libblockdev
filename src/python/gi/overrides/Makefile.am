if WITH_PYTHON2
pylibdir = $(shell python2 -c "import distutils.sysconfig; print(distutils.sysconfig.get_python_lib(1,0,prefix='${exec_prefix}'))")

overridesdir = $(pylibdir)/gi/overrides
dist_overrides_DATA = BlockDev.py
endif

if WITH_PYTHON3
py3libdir = $(shell python3 -c "import distutils.sysconfig; print(distutils.sysconfig.get_python_lib(1,0,prefix='${exec_prefix}'))")
py3overridesdir = $(py3libdir)/gi/overrides
nodist_py3overrides_DATA = BlockDev.py
endif

dist_noinst_SOURCES = __init__.py

MAINTAINERCLEANFILES = Makefile.in
