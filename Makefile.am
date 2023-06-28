SHELL = /bin/bash
.SHELLFLAGS = -o pipefail -c

ACLOCAL_AMFLAGS = -I m4
DISTCHECK_CONFIGURE_FLAGS = --enable-introspection

if !WITH_BTRFS
DISTCHECK_CONFIGURE_FLAGS += --without-btrfs
endif

if !WITH_CRYPTO
DISTCHECK_CONFIGURE_FLAGS += --without-crypto
endif

if !WITH_DM
DISTCHECK_CONFIGURE_FLAGS += --without-dm
endif

if !WITH_LOOP
DISTCHECK_CONFIGURE_FLAGS += --without-loop
endif

if !WITH_LVM
DISTCHECK_CONFIGURE_FLAGS += --without-lvm
endif

if !WITH_LVM_DBUS
DISTCHECK_CONFIGURE_FLAGS += --without-lvm-dbus
endif

if !WITH_MDRAID
DISTCHECK_CONFIGURE_FLAGS += --without-mdraid
endif

if !WITH_MPATH
DISTCHECK_CONFIGURE_FLAGS += --without-mpath
endif

if !WITH_NVDIMM
DISTCHECK_CONFIGURE_FLAGS += --without-nvdimm
endif

if !WITH_NVME
DISTCHECK_CONFIGURE_FLAGS += --without-nvme
endif

if !WITH_SWAP
DISTCHECK_CONFIGURE_FLAGS += --without-swap
endif

if !WITH_PART
DISTCHECK_CONFIGURE_FLAGS += --without-part
endif

if !WITH_S390
DISTCHECK_CONFIGURE_FLAGS += --without-s390
endif


SUBDIRS = include src dist scripts data tools
if WITH_GTK_DOC
SUBDIRS += docs
endif

dist_noinst_DATA = LICENSE tests

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 config.guess config.sub \
    configure depcomp install-sh ltmain.sh missing py-compile compile ar-lib \
    m4/*.m4

LIBDIRS = src/utils/.libs:src/plugins/.libs:src/plugins/fs/.libs:src/plugins/nvme/.libs:src/lib/.libs
GIDIR = src/lib

if WITH_PYTHON3
PYTHON = python3
else
PYTHON = python
endif

TEST_PYTHON ?= $(PYTHON)
COVERAGE ?= coverage

TEST_SUITE_LOG ?= test-suite-$(TEST_PYTHON).log
PYLINT_LOG ?= pylint.log

PLUGINS = btrfs \
	crypto \
	dm \
	fs \
	loop \
	lvm \
	mdraid \
	mpath \
	nvdimm \
	nvme \
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
	@$(PYTHON) -m pylint --version >/dev/null 2>&1; \
	if test $$? != 0 ; then \
		echo "pylint not available, skipping" ; \
	else \
		$(PYTHON) -m pylint -E src/python/gi/overrides/BlockDev.py ; \
	fi

if TESTS_ENABLED
test: all pylint
	sudo $(TEST_PYTHON) tests/run_tests.py $(TEST_ARGS)

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
	@sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/test_configs/default_config \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p '*_test.py' 2>&1 | tee -a $(TEST_SUITE_LOG)
	@sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/lvm_dbus_config \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p 'lvm_dbus_tests.py' 2>&1 | tee -a $(TEST_SUITE_LOG)
		$(COVERAGE) report --show-missing --include="src/*"
		$(COVERAGE) report --include="src/*" > coverage-report.log

coverage-all: all
	@sudo env FEELING_LUCKY= GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/test_configs/default_config \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p '*_test.py'
	@sudo env GI_TYPELIB_PATH=${GIDIR} LD_LIBRARY_PATH=${LIBDIRS} PYTHONPATH=.:tests/:src/python LIBBLOCKDEV_CONFIG_DIR=tests/lvm_dbus_config \
		$(COVERAGE) run --branch -m unittest discover -v -s tests/ -p 'lvm_dbus_tests.py'
		$(COVERAGE) report --show-missing --include="src/*"
		$(COVERAGE) report --include="src/*" > coverage-report.log

tag:
	TAG="$(PACKAGE_VERSION)-1" ; \
	git tag -a -s -m "Tag as $$TAG" -f "$$TAG" && \
	echo "Tagged as $$TAG"

rpmlog:
	@cl=`grep -n %changelog dist/libblockdev.spec.in |cut -d : -f 1 |head -1` ; \
	version_release=`tail --lines=+$$(($$cl + 1)) dist/libblockdev.spec.in|head -1|cut -d- -f2-3|sed 's/ //g'` ; \
	git log --no-merges --pretty="format:- %s (%ae)" "$$version_release.." |sed -e 's/@.*)/)/' ; \
	echo

shortlog:
	git shortlog -r --no-merges --format="- %s" -w76,0,2 $$(git tag -l --sort=taggerdate | tail -n 1)..HEAD | sed 's/"):$"/"):\n"/g'

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
