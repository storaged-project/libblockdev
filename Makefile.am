SHELL = /bin/bash
.SHELLFLAGS = -o pipefail -c

ACLOCAL_AMFLAGS = -I m4
DISTCHECK_CONFIGURE_FLAGS = --enable-introspection

SUBDIRS = include src dist scripts data
if WITH_GTK_DOC
SUBDIRS += docs
endif

dist_noinst_DATA = features.rst roadmap.rst specs.rst LICENSE tests

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 config.guess config.sub \
    configure depcomp install-sh ltmain.sh missing py-compile compile ar-lib \
    m4/*.m4

LIBDIRS = src/utils/.libs:src/plugins/.libs:src/lib/.libs
GIDIR = src/lib

if WITH_PYTHON3
PYTHON = python3
PYLINT = python3-pylint
else
PYTHON = python
PYLINT = pylint
endif

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
UTILS_TESTS = utils

run-ipython: all
	GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=src/python G_MESSAGES_DEBUG=all LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ i${PYTHON}

run-root-ipython: all
	sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=src/python G_MESSAGES_DEBUG=all LIBBLOCKDEV_CONFIG_DIR=data/conf.d/ i${PYTHON}

pylint:
	which $(PYLINT) && $(PYLINT) -E src/python/gi/overrides/BlockDev.py || echo "pylint not available, skipping"

if TESTS_ENABLED
test: all pylint
	sudo $(TEST_PYTHON) tests/run_tests.py

fast-test: all pylint
	sudo $(TEST_PYTHON) tests/run_tests.py --fast

test-all: all pylint
	sudo $(TEST_PYTHON) tests/run_tests.py --lucky

test-plugin-%: all
	sudo $(TEST_PYTHON) tests/run_tests.py $*_test

fast-test-plugin-%: all
	sudo $(TEST_PYTHON) tests/run_tests.py --fast $*_test

test-lvm-dbus: all
	sudo $(TEST_PYTHON) tests/run_tests.py lvm_dbus_tests

fast-test-lvm-dbus: all
	sudo $(TEST_PYTHON) tests/run_tests.py --fast lvm_dbus_tests
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
	TAG="$(PACKAGE_VERSION)-1" ; \
	git tag -a -s -m "Tag as $$TAG" -f "$$TAG" ; \
	echo "Tagged as $$TAG"

rpmlog:
	@cl=`grep -n %changelog dist/libblockdev.spec.in |cut -d : -f 1 |head -1` ; \
	version_release=`tail --lines=+$$(($$cl + 1)) dist/libblockdev.spec.in|head -1|cut -d- -f2-3|sed 's/ //g'` ; \
	git log --no-merges --pretty="format:- %s (%ae)" "$(PACKAGE_NAME)-$$version_release.." |sed -e 's/@.*)/)/' ; \
	echo

shortlog:
	git shortlog -r --no-merges --format="- %s" -w76,0,2 $$(git tag -l | tail -n 1)..HEAD | sed s/"):$"/"):\n"/g

bumpver:
	@VERSION=`echo $(PACKAGE_VERSION)|sed -r 's/(.*)\.([0-9]+)/\1/'` ; \
	SUBVERSION=`echo $(PACKAGE_VERSION)|sed -r 's/.*\.([0-9]+)/\1/'` ; \
	NEWSUBVERSION=$$(($$SUBVERSION + 1)) ; \
	DATELINE="* `date "+%a %b %d %Y"` `git config user.name` <`git config user.email`> - $$VERSION.$$NEWSUBVERSION-1"  ; \
	cl=`grep -n %changelog dist/libblockdev.spec.in |cut -d : -f 1 |head -1` ; \
	tail --lines=+$$(($$cl + 1)) dist/libblockdev.spec.in > speclog ; \
	(head -n $$cl dist/libblockdev.spec.in ; echo "$$DATELINE" ; make --quiet rpmlog 2>/dev/null ; echo ""; cat speclog) > dist/libblockdev.spec.in.new ; \
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

ci: all distcheck
	$(MAKE) pylint > $(PYLINT_LOG)
	$(MAKE) --quiet test
