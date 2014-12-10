PKGNAME=libblockdev
VERSION=$(shell awk '/Version:/ { print $$2 }' dist/$(PKGNAME).spec)
RELEASE=$(shell awk '/Release:/ { print $$2 }' dist/$(PKGNAME).spec | sed -e 's|%.*$$||g')
TAG=libblockdev-$(VERSION)-$(RELEASE)

PLUGIN_TESTS = test-btrfs test-lvm test-loop test-swap

all: build

build:
	scons -Q build

install:
	scons -Q --prefix=${PREFIX} install

uninstall:
	scons -Q -c install

test_%:
	scons -Q build/$@

test-%: test_%
	@echo "***Running tests***"
	LD_LIBRARY_PATH=build/ build/$<

%.so:
	scons -Q build/$@

BlockDev-1.0.gir:
	scons -Q BlockDev-1.0.gir

BlockDev-1.0.typelib:
	scons -Q BlockDev-1.0.typelib

plugins-test: ${PLUGIN_TESTS}

test-from-python: all
	GI_TYPELIB_PATH=build LD_LIBRARY_PATH=build python -c 'from gi.repository import BlockDev; BlockDev.init(None, None); print BlockDev.lvm_get_max_lv_size()'

run-ipython: all
	GI_TYPELIB_PATH=build/ LD_LIBRARY_PATH=build ipython

run-root-ipython: all
	sudo GI_TYPELIB_PATH=build/ LD_LIBRARY_PATH=build ipython

test: all
	@echo
	@sudo GI_TYPELIB_PATH=build LD_LIBRARY_PATH=build PYTHONPATH=.:tests/ \
		python -m unittest discover -v -s tests/ -p '*_test.py'

fast-test: all
	@echo
	@sudo SKIP_SLOW= GI_TYPELIB_PATH=build LD_LIBRARY_PATH=build PYTHONPATH=.:tests/ \
		python -m unittest discover -v -s tests/ -p '*_test.py'

clean:
	-rm -rf build/
	find . -name '*.pyc' -exec rm -f {} \;
	find . -name '*.pyo' -exec rm -f {} \;

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
	sed -ri "s/Version:(\\s+)$(VERSION)/Version:\\1$$NEWVERSION/" dist/libblockdev.spec ; \

archive:
	git archive --format=tar.gz --prefix=$(PKGNAME)-$(VERSION)/ -o $(PKGNAME)-$(VERSION).tar.gz $(TAG)

local:
	git archive --format=tar.gz --prefix=$(PKGNAME)-$(VERSION)/ -o $(PKGNAME)-$(VERSION).tar.gz HEAD

release: archive tag
