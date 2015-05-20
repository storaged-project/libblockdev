ACLOCAL_AMFLAGS = -I m4
DISTCHECK_CONFIGURE_FLAGS = --enable-introspection

SUBDIRS = src docs dist

dist_noinst_DATA = features.rst roadmap.rst specs.rst LICENSE

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 config.guess config.sub \
    configure depcomp install-sh ltmain.sh missing py-compile compile ar-lib \
    m4/*.m4

LIBDIRS = src/utils/.libs:src/plugins/.libs:src/lib/.libs
GIDIR = src/lib

run-ipython: all
	GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=src/python G_MESSAGES_DEBUG=all ipython

run-root-ipython: all
	sudo GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=src/python G_MESSAGES_DEBUG=all ipython

test: all
	pylint -E src/python/gi/overrides/BlockDev.py
	@sudo GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python \
		python -m unittest discover -v -s tests/ -p '*_test.py'

fast-test: all
	pylint -E src/python/gi/overrides/BlockDev.py
	@sudo SKIP_SLOW= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python \
		python -m unittest discover -v -s tests/ -p '*_test.py'

test-all: all
	pylint -E src/python/gi/overrides/BlockDev.py
	@sudo FEELINGLUCKY= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python \
		python -m unittest discover -v -s tests/ -p '*_test.py'

tag:
	git tag -a -s -m "Tag as $(TAG)" -f $(TAG)
	@echo "Tagged as $(TAG)"

rpmlog:
	@git log --pretty="format:- %s (%ae)" $(TAG).. |sed -e 's/@.*)/)/'
	@echo

bumpver:
	@NEWSUBVER=$$((`echo $(VERSION) |cut -d . -f 2` + 1)) ; \
	NEWVERSION=`echo $(VERSION).$$NEWSUBVER |cut -d . -f 1,3` ; \
	DATELINE="* `date "+%a %b %d %Y"` `git config user.name` <`git config user.email`> - $$NEWVERSION-1"  ; \
	cl=`grep -n %changelog dist/libblockdev.spec |cut -d : -f 1` ; \
	tail --lines=+$$(($$cl + 1)) dist/libblockdev.spec > speclog ; \
	(head -n $$cl dist/libblockdev.spec ; echo "$$DATELINE" ; make --quiet rpmlog 2>/dev/null ; echo ""; cat speclog) > dist/libblockdev.spec.new ; \
	mv dist/libblockdev.spec.new dist/libblockdev.spec ; rm -f speclog ; \
	sed -ri "s/Version:(\\s+)$(VERSION)/Version:\\1$$NEWVERSION/" dist/libblockdev.spec ;

archive: distcheck

local: dist

srpm: local
	rpmbuild -ts --nodeps $(PKGNAME)-$(VERSION).tar.gz
	rm -f $(PKGNAME)-$(VERSION).tar.gz

rpm: local
	rpmbuild -tb --nodeps $(PKGNAME)-$(VERSION).tar.gz
	rm -f $(PKGNAME)-$(VERSION).tar.gz

release: tag
	$(MAKE) archive
