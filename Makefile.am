ACLOCAL_AMFLAGS = -I m4
DISTCHECK_CONFIGURE_FLAGS = --enable-introspection

SUBDIRS = src docs dist scripts data

dist_noinst_DATA = features.rst roadmap.rst specs.rst LICENSE

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 config.guess config.sub \
    configure depcomp install-sh ltmain.sh missing py-compile compile ar-lib \
    m4/*.m4

LIBDIRS = src/utils/.libs:src/plugins/.libs:src/lib/.libs
GIDIR = src/lib

TEST_PYTHON ?= $(PYTHON)
COVERAGE ?= coverage

run-ipython: all
	GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=src/python G_MESSAGES_DEBUG=all LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ ipython

run-root-ipython: all
	sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=src/python G_MESSAGES_DEBUG=all LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ ipython

check: all
	pylint -E src/python/gi/overrides/BlockDev.py

test: all check
	@sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ \
		$(TEST_PYTHON) -m unittest discover -v -s tests/ -p '*_test.py'

fast-test: all check
	@sudo env SKIP_SLOW= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ \
		$(TEST_PYTHON) -m unittest discover -v -s tests/ -p '*_test.py'

test-all: all check
	@sudo env FEELINGLUCKY= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ \
		$(TEST_PYTHON) -m unittest discover -v -s tests/ -p '*_test.py'

coverage: all
	@sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p '*_test.py'
		$(COVERAGE) report --show-missing --include="src/*"
		$(COVERAGE) report --include="src/*" > coverage-report.log

coverage-all: all
	@sudo env FEELING_LUCKY= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p '*_test.py'
		$(COVERAGE) report --show-missing --include="src/*"
		$(COVERAGE) report --include="src/*" > coverage-report.log

tag:
	@TAG="$(PACKAGE_NAME)-$(PACKAGE_VERSION)-1" ; \
	git tag -a -s -m "Tag as $$TAG" -f "$$TAG" ; \
	echo "Tagged as $$TAG"

rpmlog:
	@cl=`grep -n %changelog dist/libblockdev.spec.in |cut -d : -f 1` ; \
	version_release=`tail --lines=+$$(($$cl + 1)) dist/libblockdev.spec.in|head -1|cut -d- -f2-3|sed 's/ //g'` ; \
	git log --pretty="format:- %s (%ae)" "$(PACKAGE_NAME)-$$version_release.." |sed -e 's/@.*)/)/' ; \
	echo

bumpver:
	@VERSION=`echo $(PACKAGE_VERSION)|sed -r 's/(.*)\.([0-9]+)/\1/'` ; \
	SUBVERSION=`echo $(PACKAGE_VERSION)|sed -r 's/.*\.([0-9]+)/\1/'` ; \
	NEWSUBVERSION=$$(($$SUBVERSION + 1)) ; \
	DATELINE="* `date "+%a %b %d %Y"` `git config user.name` <`git config user.email`> - $$VERSION.$$NEWSUBVERSION-1"  ; \
	cl=`grep -n %changelog dist/libblockdev.spec.in |cut -d : -f 1` ; \
	tail --lines=+$$(($$cl + 1)) dist/libblockdev.spec.in > speclog ; \
	(head -n $$cl dist/libblockdev.spec ; echo "$$DATELINE" ; make --quiet rpmlog 2>/dev/null ; echo ""; cat speclog) > dist/libblockdev.spec.in.new ; \
	mv dist/libblockdev.spec.in.new dist/libblockdev.spec.in ; rm -f speclog ; \
	sed -ri "s/(AC_INIT\(\[$(PACKAGE_NAME)\], \[)[0-9]+\.[0-9]+(\],.*)/\1$$VERSION.$$NEWSUBVERSION\2/" configure.ac ; \
	sed -ri "s/Version:(\\s+)[-0-9.]+/Version:\\1$$VERSION.$$NEWSUBVERSION/" dist/libblockdev.spec.in ;

archive: distcheck

local: dist

srpm: local
	rpmbuild -ts --nodeps $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz
	rm -f $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz

rpm: local
	rpmbuild -tb --nodeps $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz
	rm -f $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz

release: tag
	$(MAKE) archive

ci: distcheck
	TEST_PYTHON=python2 $(MAKE) test
	COVERAGE=coverage3 $(MAKE) coverage
