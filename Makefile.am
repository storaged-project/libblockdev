SHELL = /bin/bash
.SHELLFLAGS = -o pipefail -c

ACLOCAL_AMFLAGS = -I m4
DISTCHECK_CONFIGURE_FLAGS = --enable-introspection

SUBDIRS = src docs dist scripts data

dist_noinst_DATA = features.rst roadmap.rst specs.rst LICENSE tests

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 config.guess config.sub \
    configure depcomp install-sh ltmain.sh missing py-compile compile ar-lib \
    m4/*.m4

LIBDIRS = src/utils/.libs:src/plugins/.libs:src/lib/.libs
GIDIR = src/lib

PYTHON = python3

TEST_PYTHON ?= $(PYTHON)
COVERAGE ?= coverage

TEST_SUITE_LOG ?= test-suite-$(TEST_PYTHON).log
PYLINT_LOG ?= pylint.log

PLUGINS = btrfs \
	crypto \
	dm \
	fs \
	kbd \
	loop \
	lvm \
	mdraid \
	mpath \
	part \
	s390 \
	swap

ALT_PLUGINS = lvm-dbus

LIBRARY_TESTS = library

run-ipython: all
	GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=src/python G_MESSAGES_DEBUG=all LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ ipython3

run-root-ipython: all
	sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=src/python G_MESSAGES_DEBUG=all LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ ipython3

check: all pylint

pylint:
	python3-pylint -E src/python/gi/overrides/BlockDev.py

if TESTS_ENABLED
test: all check
	@rm -f tests/error_occurred;
	@rm -f $(TEST_SUITE_LOG)
	@touch $(TEST_SUITE_LOG)
	for plugin in ${PLUGINS} ${LIBRARY_TESTS}; do \
		$(MAKE) test-plugin-$$plugin || touch tests/error_occurred; \
	done
	$(MAKE) test-lvm-dbus || touch tests/error_occurred
	@test ! -f tests/error_occurred

fast-test: all check
	@rm -f tests/error_occurred;
	for plugin in ${PLUGINS} ${LIBRARY_TESTS}; do \
		$(MAKE) fast-test-plugin-$$plugin || touch tests/error_occurred; \
	done
	$(MAKE) fast-test-lvm-dbus || touch tests/error_occurred
	@test ! -f tests/error_occurred

test-all: all check
	sudo env FEELINGLUCKY= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python \
		$(TEST_PYTHON) -m unittest discover -v -s tests/ -p '*_test.py'
	sudo env FEELINGLUCKY= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python \
		$(TEST_PYTHON) -m unittest discover -v -s tests/ -p 'lvm_dbus_tests.py'

test-plugin-%: all
	sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/default_config \
        $(TEST_PYTHON) -m unittest discover -v -s tests/ -p $*_test.py 2>&1 | tee -a $(TEST_SUITE_LOG)

fast-test-plugin-%: all
	sudo env SKIP_SLOW= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/default_config \
		$(TEST_PYTHON) -m unittest discover -f -v -s tests/ -p $*_test.py

test-lvm-dbus: all
	sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/lvm_dbus_config \
		$(TEST_PYTHON) -m unittest discover -v -s tests/ -p 'lvm_dbus_tests.py'

fast-test-lvm-dbus: all
	sudo env SKIP_SLOW= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/lvm_dbus_config \
		$(TEST_PYTHON) -m unittest discover -f -v -s tests/ -p 'lvm_dbus_tests.py'
endif # TESTS_ENABLED

coverage: all
	@rm -f $(TEST_SUITE_LOG)
	@sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/default_config \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p '*_test.py' 2>&1 | tee -a $(TEST_SUITE_LOG)
	@sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/lvm_dbus_config \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p 'lvm_dbus_tests.py' 2>&1 | tee -a $(TEST_SUITE_LOG)
		$(COVERAGE) report --show-missing --include="src/*"
		$(COVERAGE) report --include="src/*" > coverage-report.log

coverage-all: all
	@sudo env FEELING_LUCKY= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/default_config \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p '*_test.py'
	@sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/lvm_dbus_config \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p 'lvm_dbus_tests.py'
		$(COVERAGE) report --show-missing --include="src/*"
		$(COVERAGE) report --include="src/*" > coverage-report.log

tag:
	@TAG="$(PACKAGE_NAME)-$(PACKAGE_VERSION)-1" ; \
	git tag -a -s -m "Tag as $$TAG" -f "$$TAG" ; \
	echo "Tagged as $$TAG" ; \
	TAG="$(PACKAGE_VERSION)-1" ; \
	git tag -a -s -m "Tag as $$TAG" -f "$$TAG" ; \
	echo "Tagged as $$TAG"

rpmlog:
	@cl=`grep -n %changelog dist/libblockdev.spec.in |cut -d : -f 1 |head -1` ; \
	version_release=`tail --lines=+$$(($$cl + 1)) dist/libblockdev.spec.in|head -1|cut -d- -f2-3|sed 's/ //g'` ; \
	git log --no-merges --pretty="format:- %s (%ae)" "$(PACKAGE_NAME)-$$version_release.." |sed -e 's/@.*)/)/' ; \
	echo

bumpver:
	@VERSION=`echo $(PACKAGE_VERSION)|sed -r 's/(.*)\.([0-9]+)/\1/'` ; \
	SUBVERSION=`echo $(PACKAGE_VERSION)|sed -r 's/.*\.([0-9]+)/\1/'` ; \
	NEWSUBVERSION=$$(($$SUBVERSION + 1)) ; \
	DATELINE="* `date "+%a %b %d %Y"` `git config user.name` <`git config user.email`> - $$VERSION.$$NEWSUBVERSION-1"  ; \
	cl=`grep -n %changelog dist/libblockdev.spec.in |cut -d : -f 1 |head -1` ; \
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
	$(MAKE) pylint > $(PYLINT_LOG)
	TEST_PYTHON=python3 COVERAGE=coverage3 $(MAKE) coverage
