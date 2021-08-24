if WITH_PYTHON3
py3libdir = $(shell python3 -c "import sysconfig; print(sysconfig.get_path('platlib', vars={'platbase': '${exec_prefix}'}))")
py3overridesdir = $(py3libdir)/gi/overrides
dist_py3overrides_DATA = BlockDev.py
endif

dist_noinst_SOURCES = __init__.py

MAINTAINERCLEANFILES = Makefile.in
